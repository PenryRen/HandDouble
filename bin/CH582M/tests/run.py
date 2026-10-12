"""Execute the actual cross-compiled protocol C tests in a RISC-V emulator."""
import pathlib, struct, sys
root = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(root / 'python-deps'))
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32, UC_HOOK_INTR
from unicorn.riscv_const import UC_RISCV_REG_A0
name = sys.argv[1] if len(sys.argv)>1 else 'protocol_test'
binary = (root.parent / 'build' / (name+'.elf')).read_bytes()
assert binary[:5] == b'\x7fELF\x01'
entry, phoff = struct.unpack_from('<II', binary, 24)
phsize, phnum = struct.unpack_from('<HH', binary, 42)
emu = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
emu.mem_map(0x10000, 1024*1024)
emu.mem_map(0x20000000, 128*1024)
for i in range(phnum):
    kind, offset, addr, _, filesz, memsz, _, _ = struct.unpack_from('<8I', binary, phoff+i*phsize)
    if kind == 1:
        emu.mem_write(addr, binary[offset:offset+filesz])
        if memsz > filesz: emu.mem_write(addr+filesz, bytes(memsz-filesz))
finished = False
def stop(uc, number, user):
    global finished
    finished = True
    uc.emu_stop()
emu.hook_add(UC_HOOK_INTR, stop)
emu.emu_start(entry, 0, count=200_000_000)
if not finished: raise RuntimeError('Test program did not complete')
result = emu.reg_read(UC_RISCV_REG_A0)
if result:
    print(f'FAIL: {name}.c:{result}')
    sys.exit(1)
print('PASS: '+name)
