#!/usr/bin/env python3
"""Disassemble ranges of binaries/extracted_1.22/kernel (arm64 Image).

Usage: python rv101_disasm.py <hex-offset> <length> [<hex-offset> <length> ...]
Offsets are link-time offsets from KIMAGE_TEXT_BASE (0xffffffc008000000),
i.e. equal to file offsets in the raw Image.
"""
import sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN

KERNEL = r"binaries/extracted_1.22/kernel"


def disasm(buf, base, start, length):
    md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
    md.detail = False
    code = buf[start:start + length]
    for insn in md.disasm(code, base + start):
        print("0x%016x: %-8s %s" % (insn.address, insn.mnemonic, insn.op_str))


def main():
    if len(sys.argv) < 3 or (len(sys.argv) - 1) % 2 != 0:
        print("usage: rv101_disasm.py <off> <len> [<off> <len> ...]")
        return 1
    with open(KERNEL, "rb") as f:
        buf = f.read()
    for i in range(1, len(sys.argv), 2):
        off = int(sys.argv[i], 16)
        ln = int(sys.argv[i + 1], 16)
        print("===== 0x%x len 0x%x =====" % (off, ln))
        disasm(buf, 0xffffffc008000000, off, ln)
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
