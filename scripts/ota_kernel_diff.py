#!/usr/bin/env python3
# ota_kernel_diff.py — compare the remove_waiter() implementation in two kernel Images (ELFs
# produced by vmlinux-to-elf).
#
# Purpose: verify the backport of CVE-2026-43499 (GhostLock). Output:
#   1. function address and size
#   2. whether it contains `mrs ..., sp_el0` (reading current)
#   3. the source instruction at `+0x040` (mrs sp_el0 = vulnerable; ldr [x23,#0x30] = patched)
#   4. a **byte-by-byte comparison** of the function's machine code: every difference (offset plus
#      the instruction on each side)
#   5. a de-addressed listing (function-relative offsets, call targets shown as symbol names)
#
# Depends on: capstone (pip install capstone) and binutils' nm / readelf.
# Usage:
#   python3 scripts/ota_kernel_diff.py --elf A.elf --elf B.elf
#   python3 scripts/ota_kernel_diff.py --elf A.elf --listing      # print one version's listing only
import argparse, bisect, re, struct, subprocess, sys

try:
    import capstone
except ImportError:
    sys.exit("需要 capstone：pip install capstone")


def nm_symbols(elf):
    out = subprocess.run(["nm", "-n", elf], capture_output=True, text=True).stdout
    syms = []
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 3:
            try:
                syms.append((int(p[0], 16), p[2]))
            except ValueError:
                pass
    return syms


def load_segments(elf):
    """Return [(vaddr, file_off, filesz)] to map a vaddr to a file offset."""
    out = subprocess.run(["readelf", "-lW", elf], capture_output=True, text=True).stdout
    segs = []
    for line in out.splitlines():
        m = re.match(r'\s*LOAD\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+0x[0-9a-f]+\s+'
                     r'(0x[0-9a-f]+)\s+(0x[0-9a-f]+)', line)
        if m:
            off, vaddr, filesz, memsz = (int(m.group(1), 16), int(m.group(2), 16),
                                         int(m.group(3), 16), int(m.group(4), 16))
            segs.append((vaddr, off, filesz))
    return segs


def read_vaddr(elf, segs, vaddr, size):
    with open(elf, "rb") as f:
        for base, off, filesz in segs:
            if base <= vaddr and vaddr + size <= base + filesz:
                f.seek(off + (vaddr - base))
                return f.read(size)
    raise ValueError(f"vaddr {vaddr:#x} 不在任何 LOAD segment 內")


def func_range(syms, name):
    addrs = [a for a, _ in syms]
    i = next((k for k, (_, n) in enumerate(syms) if n == name), None)
    if i is None:
        raise SystemExit(f"找不到符號 {name}")
    start = syms[i][0]
    end = syms[i + 1][0] if i + 1 < len(syms) else start + 0x1000
    return start, end


def disasm(code, vaddr, syms):
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    out = []
    for ins in md.disasm(code, vaddr):
        out.append((ins.address, ins.mnemonic, ins.op_str))
    return out


def sym_at(syms, addr):
    addrs = [a for a, _ in syms]
    i = bisect.bisect_right(addrs, addr) - 1
    if i < 0:
        return None, 0
    base, name = syms[i]
    return name, addr - base


def analyse(elf, fname="remove_waiter", listing=False):
    syms = nm_symbols(elf)
    segs = load_segments(elf)
    start, end = func_range(syms, fname)
    code = read_vaddr(elf, segs, start, end - start)
    insns = disasm(code, start, syms)

    r = {"elf": elf, "func": fname, "start": start, "size": end - start,
         "n_insn": len(insns), "code": code}
    r["mrs_sp_el0"] = [(a - start, a, o) for a, m, o in insns
                       if m == "mrs" and "sp_el0" in o]
    r["ldr_0x30"] = [(a - start, a, o) for a, m, o in insns
                     if m.startswith("ldr") and "#0x30]" in o]
    # the instruction at +0x040
    at40 = [(a - start, m, o) for a, m, o in insns if (a - start) == 0x40]
    r["at_0x40"] = at40[0][1:] if at40 else None
    r["key"] = {}
    for off in (0x40, 0x44, 0x48, 0x4c, 0x88, 0x8c, 0x210, 0x214):
        hit = [(m, o) for a, m, o in insns if (a - start) == off]
        r["key"][off] = hit[0] if hit else None
    r["insns"] = insns
    r["syms"] = syms

    print(f"=== {elf}")
    print(f"  {fname}: {start:#x}  大小 {end-start:#x} ({len(insns)} 條指令)")
    print(f"  含 mrs …, sp_el0 : {'是' if r['mrs_sp_el0'] else '否'}"
          + (f"  → {[hex(o) for o, _, _ in r['mrs_sp_el0']]}" if r["mrs_sp_el0"] else ""))
    print(f"  含 ldr …, #0x30  : {'是' if r['ldr_0x30'] else '否'}"
          + (f"  → {[hex(o) for o, _, _ in r['ldr_0x30']]}" if r["ldr_0x30"] else ""))
    print(f"  +0x040           : {r['at_0x40'][0]} {r['at_0x40'][1]}")
    for off in (0x44, 0x4c, 0x88, 0x210, 0x214):
        v = r["key"][off]
        print(f"  +{off:#05x}          : {v[0]} {v[1]}" if v else f"  +{off:#05x}          : —")
    if listing:
        print("  --- 去址化清單（相對偏移／符號化目標）---")
        for a, m, o in insns:
            rel = a - start
            tgt = re.match(r'#(0x[0-9a-f]+)$', o)
            if m in ("bl", "b") and tgt:
                name, off = sym_at(syms, int(tgt.group(1), 16))
                if name:
                    o = f"{name}" + (f"+{off:#x}" if off else "")
            print(f"    +{rel:#05x}  {m:8s} {o}")
    return r


def byte_diff(a, b):
    n = min(len(a["code"]), len(b["code"]))
    diffs = []
    for i in range(0, n, 4):
        x, y = a["code"][i:i+4], b["code"][i:i+4]
        if x != y:
            diffs.append((i, struct.unpack("<I", x)[0], struct.unpack("<I", y)[0]))
    return diffs, len(a["code"]), len(b["code"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", action="append", required=True, help="可多次指定（2 個＝比對）")
    ap.add_argument("--func", default="remove_waiter")
    ap.add_argument("--listing", action="store_true")
    args = ap.parse_args()

    res = [analyse(e, args.func, args.listing) for e in args.elf]
    if len(res) == 2:
        a, b = res
        print("\n=== 逐位元組比對 ===")
        diffs, la, lb = byte_diff(a, b)
        print(f"  大小：{la:#x} vs {lb:#x} → {'相同' if la == lb else '不同'}")
        print(f"  相異 4-byte 指令數：{len(diffs)}")
        md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
        for off, x, y in diffs:
            ia = next(md.disasm(struct.pack("<I", x), a["start"] + off))
            ib = next(md.disasm(struct.pack("<I", y), b["start"] + off))
            print(f"    +{off:#05x}: {ia.mnemonic} {ia.op_str}".ljust(44)
                  + f" ⟷  {ib.mnemonic} {ib.op_str}")


if __name__ == "__main__":
    main()
