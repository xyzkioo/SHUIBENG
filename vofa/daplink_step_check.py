"""Bounded 300->500 RPM check for this project's confirmed RAM layout.

Requires the matching ELF already flashed and OpenOCD Tcl RPC on localhost:50001.
Only the aligned volatile setpoint is written; gains are read back, never changed.
"""
import csv
from collections import deque
import json
from pathlib import Path
import socket
import statistics
import struct
import time

TARGET_ADDRESS = 0x2000000C
PID_ADDRESS = 0x200001B8
OUT = Path(__file__).resolve().parent
TAIL = b'\x00\x00\x80\x7f'


def rpc(command):
    wrapper = f'set trial_code [catch {{{command}}} trial_data]; format "%d\\n%s" $trial_code $trial_data'
    with socket.create_connection(('127.0.0.1', 50001), timeout=5) as connection:
        connection.sendall(wrapper.encode() + b'\x1a')
        response = bytearray()
        while not response.endswith(b'\x1a'):
            part = connection.recv(4096)
            if not part:
                raise RuntimeError('OpenOCD RPC closed')
            response.extend(part)
    code, message = response[:-1].decode().split('\n', 1)
    if code != '0':
        raise RuntimeError(message)
    return message.strip()


def read_floats(address, count):
    words = [int(value, 0) for value in rpc(f'read_memory {address:#x} 32 {count}').split()]
    return struct.unpack(f'<{count}f', struct.pack(f'<{count}I', *words))


def set_target(value):
    if rpc('stm32f1x.cpu curstate') != 'running':
        raise RuntimeError('MCU must be running; this trial never halts the CPU')
    word = struct.unpack('<I', struct.pack('<f', value))[0]
    rpc(f'mww {TARGET_ADDRESS:#x} {word:#x}')
    if read_floats(TARGET_ADDRESS, 1)[0] != value:
        raise RuntimeError('Setpoint readback mismatch')


def read_frame(reader):
    frame = reader.read(16)
    if len(frame) != 16 or frame[12:] != TAIL:
        raise RuntimeError('Missing/invalid live telemetry; stop trial')
    return struct.unpack('<3f', frame[:12])


def main():
    gains = read_floats(PID_ADDRESS, 3)
    if not all(abs(a - b) < 1e-6 for a, b in zip(gains, (5, .05, 0))):
        raise RuntimeError(f'Unexpected live gains {gains}; do not start trial')
    rows = []
    result = {'gains_readback': gains, 'controller': 'counts/50ms; u=Kp*e+Ki*sum(e)+Kd*delta(e)',
              'sample_time_s': .05, 'setpoint_rpm': 500, 'allowed_steady_rpm': [475, 525],
              'maximum_peak_rpm': 525, 'gains_changed': False,
              'timestamp_basis': 'nominal MCU 50ms period; host times preserved separately'}
    try:
        with socket.create_connection(('127.0.0.1', 8765), timeout=4) as connection:
            connection.settimeout(4)
            reader = connection.makefile('rb')
            set_target(300)
            window = deque(maxlen=60)
            settle_start = time.monotonic()
            while time.monotonic() - settle_start < 25:
                frame = read_frame(reader)
                if frame[0] == 300:
                    window.append(frame)
                if len(window) == 60 and all(abs(r[1] - 300) <= 15 for r in window):
                    break
            else:
                raise RuntimeError('300 RPM initial condition did not settle within 25 seconds')
            result['prestep_actual_mean_rpm'] = statistics.mean(r[1] for r in window)
            print(json.dumps({'prestep': result}), flush=True)
            set_target(500)
            start = time.monotonic()
            while len(rows) < 500:
                if time.monotonic() - start > 35:
                    raise RuntimeError('Telemetry too slow for a comparable 25-second trial')
                target, actual, pwm = read_frame(reader)
                if target != 500:
                    continue  # Discard old queued 300 RPM frames.
                rows.append((len(rows) * 50, target, actual, pwm, *gains,
                             round((time.monotonic() - start) * 1000, 3)))
                if actual > 650 or actual < -50:
                    raise RuntimeError('Large unexpected speed excursion; restore target')
                if len(rows) % 100 == 0:
                    print(json.dumps({'samples': len(rows), 'actual_rpm': actual, 'pwm': pwm}), flush=True)
        tail = rows[-200:]
        result.update({'source': 'measured_hardware', 'samples': len(rows),
                       'actual_mean_rpm': statistics.mean(r[2] for r in tail),
                       'tail_min_rpm': min(r[2] for r in tail),
                       'tail_max_rpm': max(r[2] for r in tail),
                       'step_peak_rpm': max(r[2] for r in rows),
                       'tail_mean_abs_error_rpm': statistics.mean(abs(r[2] - 500) for r in tail),
                       'tail_pwm_mean': statistics.mean(r[3] for r in tail)})
        result['accepted'] = (result['step_peak_rpm'] <= 525 and
                              result['tail_min_rpm'] >= 475 and result['tail_max_rpm'] <= 525)
        result['decision'] = 'hold existing gains' if result['accepted'] else 'needs further investigation'
    finally:
        set_target(500)
        result['final_target_rpm'] = read_floats(TARGET_ADDRESS, 1)[0]
        result['final_gains_readback'] = read_floats(PID_ADDRESS, 3)
        with (OUT / 'stable-step-kp5.csv').open('w', newline='', encoding='utf-8') as output:
            writer = csv.writer(output)
            writer.writerow(('timestamp_ms', 'setpoint', 'input', 'pwm', 'p', 'i', 'd', 'host_elapsed_ms'))
            writer.writerows(rows)
        (OUT / 'stable-step-kp5.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
