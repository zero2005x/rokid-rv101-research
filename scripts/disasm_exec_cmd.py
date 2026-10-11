"""Focused disassembly of run_exec_cmd_ta (0x2A70-0x2E4C) from GateServiced binary"""
import struct

data = open(r'vendor_extracted\bin\GateServiced', 'rb').read()

# PLT names
plt_base = 0x3680
rela_plt_off = 0xa58
rela_plt_size = 0x3c0
dynstr_off = 0x7dc
dynsym_off = 0x330
plt_names = {}
for i in range(rela_plt_size // 24):
    base = rela_plt_off + i * 24
    r_info = struct.unpack_from('<Q', data, base + 8)[0]
    sym_idx = r_info >> 32
    sym_base = dynsym_off + sym_idx * 24
    st_name = struct.unpack_from('<I', data, sym_base)[0]
    name_end = data.find(b'\x00', dynstr_off + st_name)
    name = data[dynstr_off + st_name : name_end].decode('ascii', errors='ignore')
    plt_names[plt_base + 32 + i * 16] = name

func_names = {
    0x23f0: 'handle_sigint',
    0x2440: 'main',
    0x263c: 'cmd_dispatch',
    0x2808: 'get_file_size',
    0x2954: 'read_file',
    0x2a70: 'run_exec_cmd_ta',
    0x2e4c: 'run_load_ta',
    0x3354: 'run_unload_ta',
    0x346c: 'dma_alloc',
    0x35d0: 'dma_unalloc',
}

# Rodata strings
rodata_off = 0xe18
rodata_size = 0xbf2
rodata_strings = {}
rd = data[rodata_off:rodata_off+rodata_size]
i = 0
while i < len(rd):
    start_i = i
    s = b''
    while i < len(rd) and rd[i] != 0 and 0x20 <= rd[i] < 0x7f:
        s += bytes([rd[i]])
        i += 1
    if len(s) >= 3:
        rodata_strings[rodata_off + start_i] = s.decode('ascii')
    i += 1

# AArch64 decoder
def decode_aarch64(addr, insn):
    """Decode common AArch64 instructions."""
    result = ""
    
    # BL (branch with link)
    if (insn >> 26) == 0x25:
        imm26 = insn & 0x3FFFFFF
        if imm26 & (1 << 25): imm26 -= (1 << 26)
        target = addr + imm26 * 4
        tgt_name = func_names.get(target, plt_names.get(target, f"0x{target:X}"))
        return f"BL {tgt_name}"

    # B (unconditional branch)
    if (insn >> 26) == 0x05:
        imm26 = insn & 0x3FFFFFF
        if imm26 & (1 << 25): imm26 -= (1 << 26)
        target = addr + imm26 * 4
        return f"B 0x{target:04x}"
    
    # B.cond
    if (insn & 0xFF000010) == 0x54000000:
        imm19 = (insn >> 5) & 0x7FFFF
        if imm19 & (1 << 18): imm19 -= (1 << 19)
        target = addr + imm19 * 4
        cond = insn & 0xF
        conds = ['EQ','NE','CS','CC','MI','PL','VS','VC','HI','LS','GE','LT','GT','LE','AL','NV']
        return f"B.{conds[cond]} 0x{target:04x}"
    
    # CBZ/CBNZ
    if (insn & 0x7E000000) == 0x34000000:
        sf = (insn >> 31) & 1
        op = (insn >> 24) & 1  # 0=CBZ, 1=CBNZ
        imm19 = (insn >> 5) & 0x7FFFF
        if imm19 & (1 << 18): imm19 -= (1 << 19)
        target = addr + imm19 * 4
        rt = insn & 0x1F
        reg = f"X{rt}" if sf else f"W{rt}"
        op_name = "CBNZ" if op else "CBZ"
        return f"{op_name} {reg}, 0x{target:04x}"
    
    # TBNZ/TBZ
    if (insn & 0x7E000000) == 0x36000000:
        op = (insn >> 24) & 1  # 0=TBZ, 1=TBNZ
        b5 = (insn >> 31) & 1
        b40 = (insn >> 19) & 0x1F
        bit = (b5 << 5) | b40
        imm14 = (insn >> 5) & 0x3FFF
        if imm14 & (1 << 13): imm14 -= (1 << 14)
        target = addr + imm14 * 4
        rt = insn & 0x1F
        reg = f"X{rt}" if b5 else f"W{rt}"
        op_name = "TBNZ" if op else "TBZ"
        return f"{op_name} {reg}, #{bit}, 0x{target:04x}"

    # ADRP
    if (insn & 0x9F000000) == 0x90000000:
        rd = insn & 0x1F
        immlo = (insn >> 29) & 3
        immhi = (insn >> 5) & 0x7FFFF
        imm = ((immhi << 2) | immlo)
        if imm & (1 << 20): imm -= (1 << 21)
        page = (addr & ~0xFFF) + (imm << 12)
        return f"ADRP X{rd}, 0x{page:x}"
    
    # ADD immediate
    if (insn & 0x7F800000) == 0x11000000:
        sf = (insn >> 31) & 1
        sh = (insn >> 22) & 1
        imm12 = (insn >> 10) & 0xFFF
        rn = (insn >> 5) & 0x1F
        rd = insn & 0x1F
        val = imm12 << (12 if sh else 0)
        rp = 'X' if sf else 'W'
        return f"ADD {rp}{rd}, {rp}{rn}, #0x{val:x}"
    
    # SUB immediate
    if (insn & 0x7F800000) == 0x51000000:
        sf = (insn >> 31) & 1
        sh = (insn >> 22) & 1
        imm12 = (insn >> 10) & 0xFFF
        rn = (insn >> 5) & 0x1F
        rd = insn & 0x1F
        val = imm12 << (12 if sh else 0)
        rp = 'X' if sf else 'W'
        return f"SUB {rp}{rd}, {rp}{rn}, #0x{val:x}"
    
    # MOV (ORR) Xd/Wd, XZR/WZR, Xm/Wm
    if (insn & 0x7FE0FFE0) == 0x2A0003E0 or (insn & 0x7FE0FFE0) == 0xAA0003E0:
        sf = (insn >> 31) & 1
        rm = (insn >> 16) & 0x1F
        rd = insn & 0x1F
        rp = 'X' if sf else 'W'
        if rm == 31:
            return f"MOV {rp}{rd}, #0x0"
        return f"MOV {rp}{rd}, {rp}{rm}"
    
    # MOVZ
    if (insn & 0x7F800000) == 0x52800000:
        sf = (insn >> 31) & 1
        hw = (insn >> 21) & 3
        imm16 = (insn >> 5) & 0xFFFF
        rd = insn & 0x1F
        rp = 'X' if sf else 'W'
        val = imm16 << (hw * 16)
        if hw > 0:
            return f"MOV {rp}{rd}, #0x{val:x}"
        return f"MOV {rp}{rd}, #0x{imm16:x}"
    
    # MOVK
    if (insn & 0x7F800000) == 0x72800000:
        sf = (insn >> 31) & 1
        hw = (insn >> 21) & 3
        imm16 = (insn >> 5) & 0xFFFF
        rd = insn & 0x1F
        rp = 'X' if sf else 'W'
        return f"MOVK {rp}{rd}, #0x{imm16:x}, LSL #{hw*16}"
    
    # LDR (unsigned offset)
    if (insn & 0x3B400000) == 0x39400000:
        size = (insn >> 30) & 3
        opc = (insn >> 22) & 3
        imm12 = (insn >> 10) & 0xFFF
        rn = (insn >> 5) & 0x1F
        rt = insn & 0x1F
        scale = size
        offset = imm12 << scale
        rp = 'X' if size == 3 else ('W' if size == 2 else 'B' if size == 0 else '?')
        if opc == 1:
            op = "LDR"
        elif opc == 0:
            op = "STR"
        else:
            op = f"LDR/STR?(opc={opc})"
        if rp == 'B' and opc == 1:
            op = "LDRB"
            rp = 'W'
        elif rp == 'B' and opc == 0:
            op = "STRB"
            rp = 'W'
        return f"{op} {rp}{rt}, [X{rn}, #0x{offset:x}]"
    
    # STR (unsigned offset) 64-bit
    if (insn & 0xFFC00000) == 0xF9000000:
        imm12 = (insn >> 10) & 0xFFF
        rn = (insn >> 5) & 0x1F
        rt = insn & 0x1F
        offset = imm12 * 8
        return f"STR X{rt}, [X{rn}, #0x{offset:x}]"
    
    # STR (unsigned offset) 32-bit
    if (insn & 0xFFC00000) == 0xB9000000:
        imm12 = (insn >> 10) & 0xFFF
        rn = (insn >> 5) & 0x1F
        rt = insn & 0x1F
        offset = imm12 * 4
        return f"STR W{rt}, [X{rn}, #0x{offset:x}]"
    
    # LDR 64-bit unsigned
    if (insn & 0xFFC00000) == 0xF9400000:
        imm12 = (insn >> 10) & 0xFFF
        rn = (insn >> 5) & 0x1F
        rt = insn & 0x1F
        offset = imm12 * 8
        return f"LDR X{rt}, [X{rn}, #0x{offset:x}]"
    
    # LDR 32-bit unsigned
    if (insn & 0xFFC00000) == 0xB9400000:
        imm12 = (insn >> 10) & 0xFFF
        rn = (insn >> 5) & 0x1F
        rt = insn & 0x1F
        offset = imm12 * 4
        return f"LDR W{rt}, [X{rn}, #0x{offset:x}]"
    
    # STP/LDP
    if (insn & 0x7FC00000) == 0x29000000 or (insn & 0x7FC00000) == 0x29400000:
        sf = (insn >> 31) & 1
        opc = "LDP" if (insn >> 22) & 1 else "STP"
        imm7 = (insn >> 15) & 0x7F
        if imm7 & 0x40: imm7 -= 128
        rt2 = (insn >> 10) & 0x1F
        rn = (insn >> 5) & 0x1F
        rt = insn & 0x1F
        scale = 3 if sf else 2
        offset = imm7 << scale
        rp = 'X' if sf else 'W'
        return f"{opc} {rp}{rt}, {rp}{rt2}, [X{rn}, #0x{offset:x}]"

    # BLR
    if (insn & 0xFFFFFC1F) == 0xD63F0000:
        rn = (insn >> 5) & 0x1F
        return f"BLR X{rn}"
    
    # BR
    if (insn & 0xFFFFFC1F) == 0xD61F0000:
        rn = (insn >> 5) & 0x1F
        return f"BR X{rn}"
    
    # RET
    if insn == 0xD65F03C0:
        return "RET"
    
    # CMP (SUB with Rd=XZR/WZR)
    if (insn & 0x7F800000) == 0x71000000:
        sf = (insn >> 31) & 1
        imm12 = (insn >> 10) & 0xFFF
        rn = (insn >> 5) & 0x1F
        rp = 'X' if sf else 'W'
        return f"CMP {rp}{rn}, #0x{imm12:x}"
    
    # CMP (register)
    if (insn & 0x7FE0001F) == 0x6B00001F or (insn & 0x7FE0001F) == 0xEB00001F:
        sf = (insn >> 31) & 1
        rm = (insn >> 16) & 0x1F
        rn = (insn >> 5) & 0x1F
        rp = 'X' if sf else 'W'
        return f"CMP {rp}{rn}, {rp}{rm}"

    # NOP
    if insn == 0xD503201F:
        return "NOP"
    
    return f"({insn:08x})"


# Disassemble run_exec_cmd_ta: 0x2A70 to 0x2E4C
start = 0x2A70
end = 0x2E4C

# Collect ADRP pairs for string resolution
adrp_regs = {}

print("=" * 80)
print(f"  FULL DISASSEMBLY: run_exec_cmd_ta (0x{start:04X} - 0x{end:04X})")
print(f"  Size: {end - start} bytes ({(end-start)//4} instructions)")
print("=" * 80)

for addr in range(start, end, 4):
    insn = struct.unpack_from('<I', data, addr)[0]
    asm = decode_aarch64(addr, insn)
    
    # Track ADRP for string resolution
    if asm.startswith("ADRP"):
        parts = asm.split()
        reg = int(parts[1][1:].rstrip(','))
        val = int(parts[2], 16)
        adrp_regs[reg] = val
    
    # Resolve ADD after ADRP
    annotation = ""
    if asm.startswith("ADD"):
        parts = asm.split(",")
        if len(parts) >= 3:
            try:
                rd_str = parts[0].split()[1].strip()
                rn_str = parts[1].strip()
                rd_num = int(rd_str[1:])
                rn_num = int(rn_str[1:])
                imm_str = parts[2].strip()
                imm_val = int(imm_str.split('#')[1], 16) if '#' in imm_str else 0
                if rn_num in adrp_regs and rd_str[0] == 'X':
                    resolved = adrp_regs[rn_num] + imm_val
                    s = rodata_strings.get(resolved, "")
                    if s:
                        annotation = f'  ; → "{s[:60]}"'
                    else:
                        annotation = f"  ; → 0x{resolved:x}"
            except:
                pass
    
    print(f"  {addr:08x}: {asm:50s}{annotation}")

print("=" * 80)
