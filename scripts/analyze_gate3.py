"""Phase 3: Disassemble full handle_client + run_load_ta, resolve all string refs"""
import struct

data = open(r'vendor_extracted\bin\GateServiced', 'rb').read()

# Build PLT name map
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

# Function address map from debug symbols
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

# Build rodata string map
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

def find_string_at(addr):
    """Find the string at or encompassing this address"""
    for saddr in sorted(rodata_strings.keys()):
        slen = len(rodata_strings[saddr])
        if saddr <= addr <= saddr + slen:
            off = addr - saddr
            s = rodata_strings[saddr]
            return s[off:off+60] if off > 0 else s[:60]
    return None

text_off = 0x2000
text_data = data[text_off:text_off+0x167c]

def disasm(start, end, label=""):
    """Disassemble with annotations"""
    if label:
        print(f"\n{'='*70}")
        print(f"  {label}")
        print(f"{'='*70}")
    
    i = start - text_off
    end_i = end - text_off
    pending_adrp = {}  # rd -> (vaddr, page)
    
    while i < end_i and i < len(text_data):
        insn = struct.unpack_from('<I', text_data, i)[0]
        vaddr = text_off + i
        
        annotation = ""
        decoded = ""
        
        # ADRP
        if (insn & 0x9F000000) == 0x90000000:
            rd = insn & 0x1F
            immhi = (insn >> 5) & 0x7FFFF
            immlo = (insn >> 29) & 0x3
            imm = (immhi << 2) | immlo
            if imm & 0x100000:
                imm -= 0x200000
            page = (vaddr & ~0xFFF) + (imm << 12)
            pending_adrp[rd] = page
            decoded = f"ADRP X{rd}, 0x{page:x}"
        
        # ADD immediate
        elif (insn & 0x7F800000) == 0x11000000:
            rd = insn & 0x1F
            rn = (insn >> 5) & 0x1F
            imm12 = (insn >> 10) & 0xFFF
            sf = (insn >> 31) & 1
            reg = 'X' if sf else 'W'
            decoded = f"ADD {reg}{rd}, {reg}{rn}, #0x{imm12:x}"
            
            if rn in pending_adrp:
                full_addr = pending_adrp[rn] + imm12
                s = find_string_at(full_addr)
                if s:
                    annotation = f'  → "{s}"'
                elif full_addr in func_names:
                    annotation = f'  → {func_names[full_addr]}'
                else:
                    annotation = f'  → 0x{full_addr:x}'
        
        # BL
        elif (insn & 0xFC000000) == 0x94000000:
            imm26 = insn & 0x03FFFFFF
            if imm26 & 0x02000000:
                imm26 -= 0x04000000
            target = vaddr + imm26 * 4
            if target in plt_names:
                decoded = f"BL {plt_names[target]}"
            elif target in func_names:
                decoded = f"BL {func_names[target]}"
            else:
                decoded = f"BL 0x{target:x}"
            annotation = " ◄"
        
        # B (unconditional)
        elif (insn & 0xFC000000) == 0x14000000:
            imm26 = insn & 0x03FFFFFF
            if imm26 & 0x02000000:
                imm26 -= 0x04000000
            target = vaddr + imm26 * 4
            decoded = f"B 0x{target:x}"
        
        # B.cond
        elif (insn & 0xFF000010) == 0x54000000:
            cond = insn & 0xF
            imm19 = (insn >> 5) & 0x7FFFF
            if imm19 & 0x40000:
                imm19 -= 0x80000
            target = vaddr + imm19 * 4
            cc = ['EQ','NE','CS','CC','MI','PL','VS','VC','HI','LS','GE','LT','GT','LE'][cond]
            decoded = f"B.{cc} 0x{target:x}"
        
        # CBZ/CBNZ
        elif (insn & 0x7E000000) == 0x34000000:
            op = (insn >> 24) & 1
            rt = insn & 0x1F
            imm19 = (insn >> 5) & 0x7FFFF
            if imm19 & 0x40000:
                imm19 -= 0x80000
            target = vaddr + imm19 * 4
            sf = (insn >> 31) & 1
            reg = f"X{rt}" if sf else f"W{rt}"
            decoded = f"{'CBNZ' if op else 'CBZ'} {reg}, 0x{target:x}"
        
        # TBZ/TBNZ
        elif (insn & 0x7E000000) == 0x36000000:
            op = (insn >> 24) & 1
            rt = insn & 0x1F
            bit = ((insn >> 31) << 5) | ((insn >> 19) & 0x1F)
            imm14 = (insn >> 5) & 0x3FFF
            if imm14 & 0x2000:
                imm14 -= 0x4000
            target = vaddr + imm14 * 4
            decoded = f"{'TBNZ' if op else 'TBZ'} W{rt}, #{bit}, 0x{target:x}"
        
        # MOV (MOVZ)
        elif (insn & 0x7F800000) == 0x52800000:
            rd = insn & 0x1F
            imm16 = (insn >> 5) & 0xFFFF
            hw = (insn >> 21) & 0x3
            sf = (insn >> 31) & 1
            reg = f"X{rd}" if sf else f"W{rd}"
            val = imm16 << (hw * 16)
            decoded = f"MOV {reg}, #0x{val:x}"
            if val > 0x3000 and val < 0x4000:
                annotation = f"  ◄ QSEE error code?"
        
        # MOVK
        elif (insn & 0x7F800000) == 0x72800000:
            rd = insn & 0x1F
            imm16 = (insn >> 5) & 0xFFFF
            hw = (insn >> 21) & 0x3
            sf = (insn >> 31) & 1
            reg = f"X{rd}" if sf else f"W{rd}"
            decoded = f"MOVK {reg}, #0x{imm16:x}, LSL #{hw*16}"
        
        # CMP (SUBS with Rd=XZR/WZR)
        elif (insn & 0x7F80001F) == 0x7100001F:
            rn = (insn >> 5) & 0x1F
            imm12 = (insn >> 10) & 0xFFF
            shift = (insn >> 22) & 0x3
            sf = (insn >> 31) & 1
            reg = f"X{rn}" if sf else f"W{rn}"
            if shift == 1:
                imm12 <<= 12
            decoded = f"CMP {reg}, #0x{imm12:x}"
            annotation = f" ◄◄◄ ({imm12})"
        
        # SUBS (register) — CMP register form
        elif (insn & 0x7F20001F) == 0x6B00001F:
            rn = (insn >> 5) & 0x1F
            rm = (insn >> 16) & 0x1F
            sf = (insn >> 31) & 1
            reg = 'X' if sf else 'W'
            decoded = f"CMP {reg}{rn}, {reg}{rm}"
        
        # LDR (register offset)
        elif (insn & 0xFFC00000) == 0xB9400000:
            rt = insn & 0x1F
            rn = (insn >> 5) & 0x1F
            imm = ((insn >> 10) & 0xFFF) * 4
            decoded = f"LDR W{rt}, [X{rn}, #0x{imm:x}]"
        elif (insn & 0xFFC00000) == 0xF9400000:
            rt = insn & 0x1F
            rn = (insn >> 5) & 0x1F
            imm = ((insn >> 10) & 0xFFF) * 8
            decoded = f"LDR X{rt}, [X{rn}, #0x{imm:x}]"
        
        # STR
        elif (insn & 0xFFC00000) == 0xB9000000:
            rt = insn & 0x1F
            rn = (insn >> 5) & 0x1F
            imm = ((insn >> 10) & 0xFFF) * 4
            decoded = f"STR W{rt}, [X{rn}, #0x{imm:x}]"
        elif (insn & 0xFFC00000) == 0xF9000000:
            rt = insn & 0x1F
            rn = (insn >> 5) & 0x1F
            imm = ((insn >> 10) & 0xFFF) * 8
            decoded = f"STR X{rt}, [X{rn}, #0x{imm:x}]"
        
        # LDRB (unsigned imm)
        elif (insn & 0xFFC00000) == 0x39400000:
            rt = insn & 0x1F
            rn = (insn >> 5) & 0x1F
            imm = (insn >> 10) & 0xFFF
            decoded = f"LDRB W{rt}, [X{rn}, #0x{imm:x}]"
        
        # STRB
        elif (insn & 0xFFC00000) == 0x39000000:
            rt = insn & 0x1F
            rn = (insn >> 5) & 0x1F
            imm = (insn >> 10) & 0xFFF
            decoded = f"STRB W{rt}, [X{rn}, #0x{imm:x}]"
        
        # RET
        elif insn == 0xD65F03C0:
            decoded = "RET"
            annotation = " ◄◄◄"
        
        # BLR
        elif (insn & 0xFFFFFC1F) == 0xD63F0000:
            rn = (insn >> 5) & 0x1F
            decoded = f"BLR X{rn}"
            annotation = " ◄ indirect"
        
        # BR
        elif (insn & 0xFFFFFC1F) == 0xD61F0000:
            rn = (insn >> 5) & 0x1F
            decoded = f"BR X{rn}"
            annotation = " ◄ indirect jump"
        
        # STP pre-index
        elif (insn & 0xFFC00000) == 0xa9800000:
            rt1 = insn & 0x1F
            rn = (insn >> 5) & 0x1F
            rt2 = (insn >> 10) & 0x1F
            imm7 = (insn >> 15) & 0x7F
            if imm7 & 0x40: imm7 -= 0x80
            decoded = f"STP X{rt1}, X{rt2}, [X{rn}, #0x{imm7*8&0xffff:x}]!"
        
        # NOP
        elif insn == 0xD503201F:
            decoded = "NOP"
        
        # MOV register (ORR Rd, XZR, Rm)
        elif (insn & 0x7F2003E0) == 0x2A0003E0:
            rd = insn & 0x1F
            rm = (insn >> 16) & 0x1F
            sf = (insn >> 31) & 1
            reg = 'X' if sf else 'W'
            decoded = f"MOV {reg}{rd}, {reg}{rm}"
        
        # SUB SP immediate
        elif (insn & 0xFF0003FF) == 0xD10003FF:
            imm = (insn >> 10) & 0xFFF
            decoded = f"SUB SP, SP, #0x{imm:x}"
            annotation = f" ◄ frame={imm}"
        
        else:
            decoded = f"({insn:08x})"
        
        if not decoded:
            decoded = f"({insn:08x})"
        
        print(f"  {vaddr:08x}: {decoded:48s}{annotation}")
        i += 4

# 1. Full handle_client (0x20c0 area - need to find exact start)
# Main sets up: ADRP X22, 0x2000; ADD X22, X22, #0xc0 → handle_client = 0x20c0
# But also check if there's a function earlier

print("\n" + "#"*70)
print("# FULL FUNCTION DISASSEMBLY")
print("#"*70)

disasm(0x2048, 0x20c0, "Unknown function at 0x2048 (called from run_exec_cmd_ta)")
disasm(0x20c0, 0x23f0, "handle_client (TCP handler thread @ 0x20c0)")
disasm(0x263c, 0x2808, "cmd_dispatch (0x263c, 460 bytes)")
disasm(0x2954, 0x2a70, "read_file (0x2954, 284 bytes)")
disasm(0x2e4c, 0x3354, "run_load_ta (0x2e4c, 1288 bytes)")

# 2. Find all BL targets to read_file and get_file_size
print(f"\n{'='*70}")
print("CROSS-REFERENCES: Who calls read_file (0x2954) and get_file_size (0x2808)?")
print(f"{'='*70}")

targets_to_find = {0x2954: 'read_file', 0x2808: 'get_file_size', 0x263c: 'cmd_dispatch',
                    0x2a70: 'run_exec_cmd_ta', 0x2e4c: 'run_load_ta', 0x3354: 'run_unload_ta'}

num_insns = len(text_data) // 4
for i in range(num_insns):
    insn = struct.unpack_from('<I', text_data, i*4)[0]
    vaddr = text_off + i * 4
    if (insn & 0xFC000000) == 0x94000000:
        imm26 = insn & 0x03FFFFFF
        if imm26 & 0x02000000:
            imm26 -= 0x04000000
        target = vaddr + imm26 * 4
        if target in targets_to_find:
            # Find which function this call is in
            caller_func = "unknown"
            for faddr in sorted(func_names.keys()):
                if faddr <= vaddr:
                    caller_func = func_names[faddr]
            print(f"  0x{vaddr:08x} ({caller_func}) → BL {targets_to_find[target]} (0x{target:x})")

# 3. Search for any string containing "file" to find read_file invocations
print(f"\n{'='*70}")
print("STRINGS CONTAINING 'file' OR 'path' (file operation clues)")
print(f"{'='*70}")
for addr, s in sorted(rodata_strings.items()):
    if 'file' in s.lower() or 'path' in s.lower() or 'open' in s.lower():
        print(f"  0x{addr:04x}: \"{s}\"")

# 4. Look for port number 8341 = 0x2095 (network byte order) or 0x9520 (stored as MOVK)
print(f"\n{'='*70}")
print("PORT NUMBER 8341 ANALYSIS")
print(f"{'='*70}")
# 8341 = 0x2095, in network byte order for sin_port: 0x9520
print(f"  Port 8341 = 0x2095 decimal, network byte order = 0x9520")
print(f"  Found in main at 0x24bc: MOVK W8, #0x9520, LSL #16")
print(f"  Combined with MOV W8, #2 at 0x24b8: W8 = 0x95200002")
print(f"  This is sockaddr_in: family=AF_INET(2), port=htons(8341)=0x9520")

# 5. Analyze the protocol versioning constants
print(f"\n{'='*70}")
print("PROTOCOL CONSTANTS ANALYSIS")  
print(f"{'='*70}")
print(f"  0x10004 (MOV W9, #4; MOVK W9, #1 LSL16) at 0x265c-0x2660")
print(f"    → Used as comparison in cmd_dispatch → UNLOAD_TA command")
print(f"  0x10003 (MOV W8, #3; MOVK W8, #1 LSL16) at 0x268c-0x2690")
print(f"    → Used as comparison in cmd_dispatch → LOAD_TA command") 
print(f"  Other cmd_ids → forwarded to run_exec_cmd_ta (TA pass-through)")
print(f"")
print(f"  0x55 (stored to DMA buf at 0x2bf8) → Request magic marker")
print(f"  0xAA (CMP at 0x2e00) → Response magic marker from TA")
print(f"  These form complementary bit patterns: 0x55=01010101, 0xAA=10101010")

# 6. Error code map
print(f"\n{'='*70}")
print("ERROR CODES FOUND IN BINARY")
print(f"{'='*70}")
error_codes = []
for i in range(num_insns):
    insn = struct.unpack_from('<I', text_data, i*4)[0]
    vaddr = text_off + i * 4
    if (insn & 0x7F800000) == 0x52800000:  # MOVZ
        val = ((insn >> 5) & 0xFFFF) << (((insn >> 21) & 0x3) * 16)
        if 0x3000 <= val < 0x4000:
            rd = insn & 0x1F
            # Find surrounding function
            for faddr in sorted(func_names.keys(), reverse=True):
                if faddr <= vaddr:
                    fname = func_names[faddr]
                    break
            else:
                fname = "unknown"
            error_codes.append((vaddr, val, rd, fname))

for vaddr, val, rd, fname in sorted(error_codes):
    known = {
        0x3001: "GENERAL_ERROR",
        0x3002: "INVALID_PARAMETER", 
        0x3003: "NOT_SUPPORTED",
        0x3005: "ALLOC_FAILED",
        0x300b: "INVALID_APP",
        0x300c: "TA_NOT_LOADED",
        0x300d: "TA_DEAD",
        0x300e: "RESP_INVALID",
        0x300f: "CMD_NOT_SUPPORTED",
    }.get(val, "UNKNOWN")
    print(f"  0x{vaddr:08x} ({fname:20s}): MOV W{rd}, #0x{val:x} — {known}")
