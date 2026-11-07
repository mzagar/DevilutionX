#!/usr/bin/env python3
"""
Cemu workaround: `tweq` traps even when its condition is false.

Cemu 2.6.0 implements the PowerPC conditional-trap-word instruction `tweq`
(TO=EQ) incorrectly: it takes the trap even when the compared registers are
NOT equal. Cemu executes TW through its interpreter (`PPCInterpreter_TW`), a
cold path that the JIT falls back to, and that handler is buggy.

This matters because GCC emits `tweq` for libstdc++'s shared_ptr use-count
overflow guard, i.e. for

    if (old_use_count == 0x7FFFFFFF) __builtin_trap();

which is generated on every shared_ptr *copy*. On Cemu that means the guest
hangs (spins in the trap handler) as soon as anything copies a shared_ptr.

This script replaces every `tweq` in an executable section with `nop`
(0x60000000, i.e. `ori r0,r0,0`). The guard it removes is an unreachable
overflow check (it can only fire after 2^31-1 copies of the same control
block), so NOPing it is behaviour-neutral in practice.

IMPORTANT
  * Must run on `devilutionx.elf` BEFORE `elf2rpl` (the RPX cannot be patched
    this way) and must be re-run after every rebuild, since the ELF is
    regenerated.
  * Not needed on real hardware (Aroma). The patched binary is still correct
    there, so a single artifact works on both.
  * Remove this workaround once Cemu fixes `tweq`.

Usage:
    cemu-tweq-workaround.py <elf> [--check-only]
"""
import struct
import sys

TW_OPCODE_MASK = 0xFC000000
TW_OPCODE = 0x7C000000
XO_TW = 4  # extended opcode for tw/tweq (instruction bits 1-10)
TO_EQ = 4  # TO field: trap if equal (instruction bits 21-25)
NOP = 0x60000000  # ori r0,r0,0


def find_text_sections(data):
    """Yield (name, offset, size) for every SHT_PROGBITS executable section."""
    if data[:4] != b"\x7fELF":
        raise SystemExit("not an ELF file")
    is64 = data[4] == 2
    if is64:
        raise SystemExit("64-bit ELF not supported (Wii U is 32-bit)")
    endian = "<" if data[5] == 1 else ">"
    e_shoff, = struct.unpack_from(endian + "I", data, 0x20)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(endian + "HHH", data, 0x2E)

    def sh(i):
        off = e_shoff + i * e_shentsize
        name, typ, flags, addr, offset, size = struct.unpack_from(endian + "IIIIII", data, off)
        return name, typ, flags, addr, offset, size

    _, _, _, _, str_off, _ = sh(e_shstrndx)
    for i in range(e_shnum):
        name_off, typ, flags, addr, offset, size = sh(i)
        name_end = data.index(b"\x00", str_off + name_off)
        name = data[str_off + name_off:name_end].decode("ascii", "replace")
        if typ == 1 and (flags & 0x4) and size:  # PROGBITS, SHF_EXECINSTR
            yield name, offset, size


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    path = sys.argv[1]
    check_only = "--check-only" in sys.argv
    data = bytearray(open(path, "rb").read())

    patched = 0
    for name, offset, size in find_text_sections(data):
        for i in range(0, size & ~3, 4):
            w, = struct.unpack_from(">I", data, offset + i)
            if (w & TW_OPCODE_MASK) != TW_OPCODE:
                continue
            if ((w >> 21) & 0x1F) != TO_EQ:  # TO field
                continue
            if ((w >> 1) & 0x3FF) != XO_TW:  # extended opcode (bits 1-10)
                continue
            print(f"  {name}+0x{i:06x}: tweq -> nop (was 0x{w:08x})")
            patched += 1
            if not check_only:
                struct.pack_into(">I", data, offset + i, NOP)

    if not patched:
        print("no tweq instructions found (already patched?)")
        return
    if check_only:
        print(f"{patched} tweq instruction(s) would be patched")
        return
    open(path, "wb").write(data)
    print(f"patched {patched} tweq instruction(s) in {path}")


if __name__ == "__main__":
    main()
