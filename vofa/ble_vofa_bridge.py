"""Read the paired HC-05 BLE UART and expose JustFloat on localhost TCP."""
import argparse
import asyncio
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import struct
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.tools' / 'ble'))
STATUS_PATH = ROOT / 'vofa' / 'bridge-status.json'
BRIDGE_VERSION = 2
TAIL = b'\x00\x00\x80\x7f'
UART_SERVICE = '0000ffe0-0000-1000-8000-00805f9b34fb'
UART_NOTIFY = '0000ffe1-0000-1000-8000-00805f9b34fb'


class JustFloatParser:
    def __init__(self):
        self.pending = bytearray()

    def feed(self, data):
        self.pending.extend(data)
        frames = []
        while True:
            end = self.pending.find(TAIL)
            if end < 0:
                if len(self.pending) > 4096:
                    del self.pending[:-15]
                break
            if end >= 12:
                frame = bytes(self.pending[end - 12:end + 4])
                channels = struct.unpack('<3f', frame[:12])
                if all(math.isfinite(value) for value in channels):
                    frames.append((frame, channels))
            del self.pending[:end + 4]
        return frames


async def main(args):
    from bleak import BleakClient, BleakScanner
    from bleak.backends.device import BLEDevice

    loop = asyncio.get_running_loop()
    started = time.monotonic()
    subscribers = set()
    client_tasks = set()
    writers = set()
    state = {
        'bridge_version': BRIDGE_VERSION,
        'device': 'HC-05', 'address': args.address,
        'connection_state': 'starting', 'ble_connected': False,
        'telemetry_fresh': False, 'bytes': 0, 'frames': 0,
        'last_channels': None, 'tcp_clients': 0,
        'connection_attempts': 0, 'successful_connections': 0,
        'last_error': None, 'tcp_port': args.tcp_port,
    }
    last_frame_at = None
    last_publish = 0.0

    def publish(force=False):
        nonlocal last_publish
        now = time.monotonic()
        if not force and now - last_publish < 1:
            return
        last_publish = now
        state['elapsed_s'] = round(now - started, 2)
        state['updated_at_utc'] = datetime.now(timezone.utc).isoformat()
        age = None if last_frame_at is None else round(now - last_frame_at, 2)
        state['last_frame_age_s'] = age
        state['telemetry_fresh'] = state['ble_connected'] and age is not None and age < 5
        payload = json.dumps(state, ensure_ascii=False)
        print(payload, flush=True)
        try:
            temporary = STATUS_PATH.with_suffix('.tmp')
            temporary.write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding='utf-8')
            temporary.replace(STATUS_PATH)
        except OSError as exc:
            # A status-file viewer must not be able to stop the BLE connection.
            print(f'Status file warning: {exc}', file=sys.stderr, flush=True)

    async def serve_client(reader, writer):
        task = asyncio.current_task()
        client_tasks.add(task)
        writers.add(writer)
        queue = asyncio.Queue(maxsize=100)
        subscribers.add(queue)
        state['tcp_clients'] = len(subscribers)
        try:
            while not reader.at_eof():
                try:
                    frame = await asyncio.wait_for(queue.get(), timeout=1)
                except asyncio.TimeoutError:
                    continue  # Keep VOFA+ connected even while BLE is offline.
                writer.write(frame)
                await writer.drain()
        except (ConnectionError, asyncio.CancelledError):
            pass
        finally:
            subscribers.discard(queue)
            writers.discard(writer)
            state['tcp_clients'] = len(subscribers)
            client_tasks.discard(task)
            writer.close()
            try:
                await writer.wait_closed()
            except ConnectionError:
                pass

    # Start TCP before BLE so VOFA+ can connect during retries.
    server = None
    if args.mode == 'serve':
        server = await asyncio.start_server(serve_client, '127.0.0.1', args.tcp_port)
        state['tcp_port'] = server.sockets[0].getsockname()[1]
        print(f"VOFA+: TCP Client 127.0.0.1:{state['tcp_port']}, JustFloat", flush=True)
    publish(force=True)

    async def run_connection():
        nonlocal last_frame_at
        state['connection_attempts'] += 1
        state['connection_state'] = 'connecting'
        state['ble_connected'] = False
        state['last_channels'] = None
        last_frame_at = None
        publish(force=True)
        if args.paired and sys.platform == 'win32':
            device = BLEDevice(args.address, 'HC-05', None)
        else:
            device = await BleakScanner.find_device_by_address(args.address, timeout=10)
        if device is None:
            raise RuntimeError('HC-05 not found. Check module power and other BLE connections.')

        disconnected = asyncio.Event()
        # Bind this event to this connection; an old callback cannot cancel a new session.
        def on_disconnect(_, event=disconnected):
            loop.call_soon_threadsafe(event.set)

        client = BleakClient(device, timeout=30, winrt={'use_cached_services': True},
                             disconnected_callback=on_disconnect)
        try:
            await asyncio.wait_for(client.connect(), timeout=35)
            print(f'Connected: {device.name or "HC-05"} ({device.address})', flush=True)
            if args.mode == 'inspect':
                for service in client.services:
                    print(f'Service {service.uuid}', flush=True)
                    for characteristic in service.characteristics:
                        print(f'  {characteristic.uuid}: {characteristic.properties}', flush=True)
                return

            service = client.services.get_service(UART_SERVICE)
            characteristic = service.get_characteristic(UART_NOTIFY) if service else None
            if characteristic is None or 'notify' not in characteristic.properties:
                raise RuntimeError('FFE0/FFE1 UART notification service unavailable.')
            parser = JustFloatParser()

            def on_data(_, data):
                nonlocal last_frame_at
                if disconnected.is_set():
                    return
                state['bytes'] += len(data)
                for frame, channels in parser.feed(data):
                    last_frame_at = time.monotonic()
                    state['frames'] += 1
                    state['last_channels'] = channels
                    for queue in tuple(subscribers):
                        if queue.full():
                            queue.get_nowait()  # Drop only a complete old frame.
                        queue.put_nowait(frame)

            await asyncio.wait_for(client.start_notify(characteristic, on_data), timeout=15)
            state['successful_connections'] += 1
            state['connection_state'] = 'connected'
            state['ble_connected'] = True
            state['last_error'] = None
            publish(force=True)
            connected_at = time.monotonic()
            while not disconnected.is_set():
                try:
                    await asyncio.wait_for(disconnected.wait(), timeout=1)
                except asyncio.TimeoutError:
                    pass
                publish()
                if args.duration and time.monotonic() - connected_at >= args.duration:
                    return
            raise ConnectionError('HC-05 BLE disconnected')
        finally:
            state['ble_connected'] = False
            state['telemetry_fresh'] = False
            state['last_channels'] = None
            for queue in tuple(subscribers):
                while not queue.empty():
                    queue.get_nowait()
            try:
                await asyncio.wait_for(client.disconnect(), timeout=5)
            except Exception as exc:
                print(f'BLE cleanup: {type(exc).__name__}: {exc}', flush=True)

    retry_delay = args.retry_delay
    try:
        while True:
            try:
                await run_connection()
                break  # inspect/capture-duration completed successfully
            except Exception as exc:
                state['last_error'] = f'{type(exc).__name__}: {exc}'
                if args.mode != 'serve':
                    raise
                state['connection_state'] = 'retry_wait'
                state['retry_in_s'] = retry_delay
                publish(force=True)
                print(f'BLE offline; retrying in {retry_delay:g}s. TCP stays available.', flush=True)
                await asyncio.sleep(retry_delay)
                retry_delay = min(retry_delay * 2, 10)
    finally:
        state['connection_state'] = 'stopped'
        state['ble_connected'] = False
        state['last_channels'] = None
        state['telemetry_fresh'] = False
        if server:
            server.close()
        for writer in tuple(writers):
            writer.close()
        remaining_tasks = tuple(client_tasks)
        for task in remaining_tasks:
            task.cancel()
        if remaining_tasks:
            await asyncio.gather(*remaining_tasks, return_exceptions=True)
        if server:
            await server.wait_closed()
        publish(force=True)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--address', default='18:2B:B6:EE:0E:CE')
    ap.add_argument('--mode', choices=['inspect', 'capture', 'serve'], default='serve')
    ap.add_argument('--duration', type=float, default=0)
    ap.add_argument('--tcp-port', type=int, default=8765)
    ap.add_argument('--paired', action='store_true', help='Use a known paired Windows device without scanning')
    ap.add_argument('--retry-delay', type=float, default=2)
    options = ap.parse_args()
    if options.retry_delay <= 0:
        ap.error('--retry-delay must be positive')
    try:
        asyncio.run(main(options))
    except KeyboardInterrupt:
        pass
    except Exception as exc:
        print(f'ERROR: {type(exc).__name__}: {exc!r}', file=sys.stderr, flush=True)
        sys.exit(1)
