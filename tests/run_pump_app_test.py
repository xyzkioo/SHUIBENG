"""Cross-compile and emulate the real pump_app.c; no connected hardware needed."""
import io
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.tools/control-test'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC

GCC = Path('C:/Users/33659/AppData/Local/stm32cube/bundles/gnu-tools-for-stm32/14.3.1+st.2/bin/arm-none-eabi-gcc.exe')
out = ROOT / 'build/pump-app-test.elf'
subprocess.run([str(GCC), '-mcpu=cortex-m3', '-mthumb', '-mfloat-abi=soft',
                '-std=c11', '-Wall', '-Wextra', '-Werror', '-fno-builtin',
                '-DSTM32F103x6', '-DUSE_HAL_DRIVER', '-ICore/Inc',
                '-IDrivers/STM32F1xx_HAL_Driver/Inc',
                '-IDrivers/CMSIS/Device/ST/STM32F1xx/Include', '-IDrivers/CMSIS/Include',
                '-nostdlib', '-Ihareware', 'tests/pump_app_test.c',
                'hareware/pump_app.c', 'hareware/motion_profile.c', 'hareware/pid.c', 'hareware/bt_control.c',
                '-Wl,-Ttext=0x10000,-Tdata=0x20000000',
                '-Wl,-e,test_main', '-lm', '-lc', '-lnosys', '-lgcc', '-o', str(out)], cwd=ROOT, check=True)
elf = ELFFile(io.BytesIO(out.read_bytes()))
cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
cpu.mem_map(0x10000, 0x20000)
cpu.mem_map(0x20000000, 0x10000)
for segment in elf.iter_segments():
    if segment['p_type'] == 'PT_LOAD' and segment['p_memsz']:
        cpu.mem_write(segment['p_vaddr'], segment.data())
cpu.reg_write(UC_ARM_REG_SP, 0x2000fff0)
cpu.reg_write(UC_ARM_REG_LR, 0x2fff1)
cpu.emu_start(elf['e_entry'] | 1, 0x2fff0, timeout=20_000_000, count=20_000_000)
if cpu.reg_read(UC_ARM_REG_PC) != 0x2fff0:
    raise SystemExit('FAIL: test did not return within the instruction/time limit')
result = cpu.reg_read(UC_ARM_REG_R0)
if result:
    raise SystemExit(f'FAIL: pump_app_test.c assertion line {result}')
print('PASS: boot idle, debounce/bounce, short/long press, immediate stop, held key, '
      'encoder wrap/sign, approach/arrival, stall timeout, relative travel beyond boot limits, tick wrap, '
      'S-curve speed/acceleration/jerk bounds, short moves, bidirectional arrival, '
      'lagging motor model and low-speed arrival, 800ms long press, '
      'directional PI limits, zero-bound anti-windup, wrong-direction and tracking faults, '
      'minimum creep startup, pure PI with static-friction plant; '
      'Bluetooth handshake, fragmentation, batches, ranges, stop, heartbeat, overflow recovery')
