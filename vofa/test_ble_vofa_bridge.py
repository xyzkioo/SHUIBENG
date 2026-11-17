import asyncio
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import ble_vofa_bridge as bridge


class BridgeTests(unittest.IsolatedAsyncioTestCase):
    def test_fragmented_frames(self):
        parser = bridge.JustFloatParser()
        frame = struct.pack('<3f', 500, 496, 350) + bridge.TAIL
        self.assertEqual(parser.feed(b'noise' + frame[:7]), [])
        self.assertEqual(parser.feed(frame[7:14]), [])
        self.assertEqual([v for _, v in parser.feed(frame[14:] + frame)],
                         [(500.0, 496.0, 350.0)] * 2)

    async def test_tcp_survives_ble_drop_and_failed_retry(self):
        class FakeClient:
            attempts = 0

            def __init__(self, device, *, disconnected_callback, **kwargs):
                type(self).attempts += 1
                self.number = type(self).attempts
                self.callback = disconnected_callback
                self.task = None
                characteristic = SimpleNamespace(properties=['notify'])
                service = SimpleNamespace(get_characteristic=lambda _: characteristic)
                self.services = SimpleNamespace(get_service=lambda _: service)

            async def connect(self):
                if self.number == 2:
                    raise TimeoutError('simulated connection timeout')

            async def start_notify(self, characteristic, callback):
                async def stream():
                    await asyncio.sleep(0.08)
                    while True:
                        actual = 480 if self.number == 1 else 500
                        callback(characteristic, struct.pack('<3f', 500, actual, 350) + bridge.TAIL)
                        await asyncio.sleep(0.03)
                        if self.number == 1:
                            self.callback(self)
                            return
                self.task = asyncio.create_task(stream())

            async def disconnect(self):
                if self.task:
                    self.task.cancel()
                    await asyncio.gather(self.task, return_exceptions=True)

        args = SimpleNamespace(address='18:2B:B6:EE:0E:CE', paired=True,
                               mode='serve', tcp_port=0, duration=0.2, retry_delay=0.02)
        with tempfile.TemporaryDirectory() as directory:
            status_path = Path(directory) / 'status.json'
            with patch.object(bridge, 'STATUS_PATH', status_path), \
                 patch('bleak.BleakClient', FakeClient), redirect_stdout(io.StringIO()):
                task = asyncio.create_task(bridge.main(args))
                writer = None
                try:
                    for _ in range(100):
                        if status_path.exists():
                            state = json.loads(status_path.read_text(encoding='utf-8'))
                            if state['tcp_port']:
                                break
                        await asyncio.sleep(0.01)
                    reader, writer = await asyncio.open_connection('127.0.0.1', state['tcp_port'])
                    first = await asyncio.wait_for(reader.readexactly(16), timeout=2)
                    second = await asyncio.wait_for(reader.readexactly(16), timeout=2)
                    self.assertEqual(struct.unpack('<3f', first[:12])[1], 480)
                    self.assertEqual(struct.unpack('<3f', second[:12])[1], 500)
                    await asyncio.wait_for(task, timeout=3)
                    state = json.loads(status_path.read_text(encoding='utf-8'))
                    self.assertEqual(state['connection_attempts'], 3)
                    self.assertEqual(state['successful_connections'], 2)
                    self.assertEqual(state['connection_state'], 'stopped')
                    self.assertIsNone(state['last_channels'])
                    self.assertFalse(state['telemetry_fresh'])
                finally:
                    if writer:
                        writer.close()
                        await writer.wait_closed()
                    if not task.done():
                        task.cancel()
                        await asyncio.gather(task, return_exceptions=True)


if __name__ == '__main__':
    unittest.main()
