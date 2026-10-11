#!/usr/bin/env python3
"""Disassemble a kernel function from the raw RV101 ARM64 Image with capstone.

Usage:
    python disasm_func.py <kernel_path> <start_addr_hex> <end_addr_hex> [out_txt]
Addresses are link-time virtual addresses; file offset = addr - KIMAGE_TEXT_BASE
(KIMAGE_TEXT_BASE = 0xffffffc008000000).
"""
import sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from capstone.arm64 import ARM64_OP_REG, ARM64_OP_IMM

KIMAGE_TEXT_BASE = 0xffffffc008000000

def disasm(kernel_path, start_addr, end_addr, out_txt=None):
    data = open(kernel_path, "rb").read()
    base = start_addr - KIMAGE_TEXT_BASE
    code = data[base:end_addr - KIMAGE_TEXT_BASE]
    md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
    md.detail = True
    lines = []
    for insn in md.disasm(code, start_addr):
        opstr = insn.op_str
        # annotate registers to make analysis easier
        line = "0x%x: %-8s %s" % (insn.address, insn.mnemonic, opstr)
        lines.append(line)
        print(line)
    if out_txt:
        with open(out_txt, "w") as f:
            f.write("\n".join(lines) + "\n")
        print("[saved]", out_txt, len(lines), "insns")

if __name__ == "__main__":
    kernel = sys.argv[1]
    start = int(sys.argv[2], 16)
    end = int(sys.argv[3], 16)
    out = sys.argv[4] if len(sys.argv) > 4 else None
    disasm(kernel, start, end, out)
