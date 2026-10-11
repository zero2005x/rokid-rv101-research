"""Phase 2: Decompress .gnu_debugdata, analyze cmd_dispatch, map protocol flow"""
import struct
import lzma

data = open(r'vendor_extracted\bin\GateServiced', 'rb').read()

# === 1. Decompress .gnu_debugdata (XZ) for symbol names ===
debugdata_off = 0x44c3
debugdata_size = 0x408
debugdata = data[debugdata_off:debugdata_off+debugdata_size]

print("=" * 60)
print("DECOMPRESSED .gnu_debugdata (mini ELF with symbols)")
print("=" * 60)

try:
    decompressed = lzma.decompress(debugdata)
    print(f"Decompressed size: {len(decompressed)} bytes")
    
    # This is a mini ELF - parse its symbol table
    if decompressed[:4] == b'\x7fELF':
        print("Valid mini ELF found!")
        
        # Parse mini ELF header
        e_shoff = struct.unpack_from('<Q', decompressed, 40)[0]
        e_shentsize = struct.unpack_from('<H', decompressed, 58)[0]
        e_shnum = struct.unpack_from('<H', decompressed, 60)[0]
        e_shstrndx = struct.unpack_from('<H', decompressed, 62)[0]
        
        # Get section name string table
        shstrtab_base = e_shoff + e_shstrndx * e_shentsize
        shstrtab_offset = struct.unpack_from('<Q', decompressed, shstrtab_base + 24)[0]
        
        # Find .symtab and .strtab
        symtab_sec = None
        strtab_sec = None
        
        for i in range(e_shnum):
            base = e_shoff + i * e_shentsize
            sh_name_idx = struct.unpack_from('<I', decompressed, base)[0]
            sh_type = struct.unpack_from('<I', decompressed, base + 4)[0]
            sh_offset = struct.unpack_from('<Q', decompressed, base + 24)[0]
            sh_size = struct.unpack_from('<Q', decompressed, base + 32)[0]
            
            name_end = decompressed.find(b'\x00', shstrtab_offset + sh_name_idx)
            name = decompressed[shstrtab_offset + sh_name_idx : name_end].decode('ascii', errors='ignore')
            
            if name == '.symtab':
                symtab_sec = {'offset': sh_offset, 'size': sh_size}
            elif name == '.strtab':
                strtab_sec = {'offset': sh_offset, 'size': sh_size}
            
            if sh_size > 0:
                print(f"  Section: {name:20s} type={sh_type} off=0x{sh_offset:x} size=0x{sh_size:x}")
        
        if symtab_sec and strtab_sec:
            print(f"\n--- SYMBOL TABLE (function names with addresses!) ---")
            entry_size = 24
            count = symtab_sec['size'] // entry_size
            
            symbols = []
            for i in range(count):
                base = symtab_sec['offset'] + i * entry_size
                st_name = struct.unpack_from('<I', decompressed, base)[0]
                st_info = decompressed[base + 4]
                st_shndx = struct.unpack_from('<H', decompressed, base + 6)[0]
                st_value = struct.unpack_from('<Q', decompressed, base + 8)[0]
                st_size = struct.unpack_from('<Q', decompressed, base + 16)[0]
                
                name_end = decompressed.find(b'\x00', strtab_sec['offset'] + st_name)
                name = decompressed[strtab_sec['offset'] + st_name : name_end].decode('ascii', errors='ignore')
                
                stype = st_info & 0xf
                if name and st_value > 0:
                    type_s = {0:'NOTYPE', 1:'OBJECT', 2:'FUNC', 3:'SECTION'}.get(stype, str(stype))
                    symbols.append((st_value, name, type_s, st_size))
            
            # Sort by address
            symbols.sort(key=lambda x: x[0])
            for addr, name, stype, size in symbols:
                print(f"  0x{addr:08x} sz={size:5d} {stype:7s} {name}")
        else:
            print("No .symtab/.strtab found in mini ELF")
    else:
        print("Not a valid ELF after decompression")
        print(f"First bytes: {decompressed[:16].hex()}")
        
except Exception as e:
    print(f"Decompression failed: {e}")

# === 2. Deep analysis of handle_client area (0x2048-0x23ec) ===
print(f"\n{'='*60}")
print("HANDLE_CLIENT FUNCTION DETAILED ANALYSIS")
print(f"{'='*60}")

# The CMP W0, #3 at 0x228c is key - this is the cmd_id dispatch
# Let's trace the code around it

text_off = 0x2000
text_data = data[text_off:text_off+0x167c]

def decode_insn_basic(insn, vaddr):
    """Basic AArch64 instruction decoder for key instructions"""
    
    # MOV (immediate) - MOVZ
    if (insn & 0x7F800000) == 0x52800000:
        rd = insn & 0x1F
        imm16 = (insn >> 5) & 0xFFFF
        hw = (insn >> 21) & 0x3
        sf = (insn >> 31) & 1
        reg = f"X{rd}" if sf else f"W{rd}"
        val = imm16 << (hw * 16)
        return f"MOV {reg}, #0x{val:x}"
    
    # MOVK
    if (insn & 0x7F800000) == 0x72800000:
        rd = insn & 0x1F
        imm16 = (insn >> 5) & 0xFFFF
        hw = (insn >> 21) & 0x3
        sf = (insn >> 31) & 1
        reg = f"X{rd}" if sf else f"W{rd}"
        shift = hw * 16
        return f"MOVK {reg}, #0x{imm16:x}, LSL #{shift}"
    
    # CMP (SUB with Rd=WZR/XZR)
    if (insn & 0x7F80001F) == 0x7100001F:
        rn = (insn >> 5) & 0x1F
        imm12 = (insn >> 10) & 0xFFF
        shift = (insn >> 22) & 0x3
        sf = (insn >> 31) & 1
        reg = f"X{rn}" if sf else f"W{rn}"
        if shift == 1:
            imm12 <<= 12
        return f"CMP {reg}, #0x{imm12:x} ({imm12})"
    
    # B.cond
    if (insn & 0xFF000010) == 0x54000000:
        cond = insn & 0xF
        imm19 = (insn >> 5) & 0x7FFFF
        if imm19 & 0x40000:
            imm19 = imm19 - 0x80000
        target = vaddr + imm19 * 4
        cond_names = ['EQ','NE','CS','CC','MI','PL','VS','VC','HI','LS','GE','LT','GT','LE','AL','NV']
        return f"B.{cond_names[cond]} 0x{target:08x}"
    
    # B (unconditional)
    if (insn & 0xFC000000) == 0x14000000:
        imm26 = insn & 0x03FFFFFF
        if imm26 & 0x02000000:
            imm26 = imm26 - 0x04000000
        target = vaddr + imm26 * 4
        return f"B 0x{target:08x}"
    
    # BL
    if (insn & 0xFC000000) == 0x94000000:
        imm26 = insn & 0x03FFFFFF
        if imm26 & 0x02000000:
            imm26 = imm26 - 0x04000000
        target = vaddr + imm26 * 4
        return f"BL 0x{target:08x}"
    
    # CBZ/CBNZ
    if (insn & 0x7E000000) == 0x34000000:
        op = (insn >> 24) & 1
        rt = insn & 0x1F
        imm19 = (insn >> 5) & 0x7FFFF
        if imm19 & 0x40000:
            imm19 = imm19 - 0x80000
        target = vaddr + imm19 * 4
        sf = (insn >> 31) & 1
        reg = f"X{rt}" if sf else f"W{rt}"
        return f"{'CBNZ' if op else 'CBZ'} {reg}, 0x{target:08x}"
    
    # LDR (immediate, unsigned offset)
    if (insn & 0xFFC00000) == 0xB9400000:
        rt = insn & 0x1F
        rn = (insn >> 5) & 0x1F
        imm12 = (insn >> 10) & 0xFFF
        return f"LDR W{rt}, [X{rn}, #0x{imm12*4:x}]"
    
    if (insn & 0xFFC00000) == 0xF9400000:
        rt = insn & 0x1F
        rn = (insn >> 5) & 0x1F
        imm12 = (insn >> 10) & 0xFFF
        return f"LDR X{rt}, [X{rn}, #0x{imm12*8:x}]"
    
    # STR
    if (insn & 0xFFC00000) == 0xB9000000:
        rt = insn & 0x1F
        rn = (insn >> 5) & 0x1F
        imm12 = (insn >> 10) & 0xFFF
        return f"STR W{rt}, [X{rn}, #0x{imm12*4:x}]"
    
    # STP (pre-index)
    if (insn & 0xFFC00000) == 0xA9800000 or (insn & 0xFE400000) == 0xA9800000:
        rt1 = insn & 0x1F
        rn = (insn >> 5) & 0x1F
        rt2 = (insn >> 10) & 0x1F
        imm7 = (insn >> 15) & 0x7F
        if imm7 & 0x40:
            imm7 = imm7 - 0x80
        return f"STP X{rt1}, X{rt2}, [X{rn}, #0x{imm7*8:x}]!"
    
    # ADRP
    if (insn & 0x9F000000) == 0x90000000:
        rd = insn & 0x1F
        immhi = (insn >> 5) & 0x7FFFF
        immlo = (insn >> 29) & 0x3
        imm = (immhi << 2) | immlo
        if imm & 0x100000:
            imm = imm - 0x200000
        page = (vaddr & ~0xFFF) + (imm << 12)
        return f"ADRP X{rd}, 0x{page:08x}"
    
    # ADD (immediate)
    if (insn & 0x7F800000) == 0x11000000:
        rd = insn & 0x1F
        rn = (insn >> 5) & 0x1F
        imm12 = (insn >> 10) & 0xFFF
        sf = (insn >> 31) & 1
        reg_d = f"X{rd}" if sf else f"W{rd}"
        reg_n = f"X{rn}" if sf else f"W{rn}"
        return f"ADD {reg_d}, {reg_n}, #0x{imm12:x}"
    
    # RET
    if insn == 0xD65F03C0:
        return "RET"
    
    # BLR
    if (insn & 0xFFFFFC1F) == 0xD63F0000:
        rn = (insn >> 5) & 0x1F
        return f"BLR X{rn}"
    
    # BR
    if (insn & 0xFFFFFC1F) == 0xD61F0000:
        rn = (insn >> 5) & 0x1F
        return f"BR X{rn}"
    
    return f"(0x{insn:08x})"

# Build PLT address map
plt_base = 0x3680
rela_plt_off = 0xa58
rela_plt_size = 0x3c0
dynstr_off = 0x7dc
dynsym_off = 0x330

plt_names = {}
entry_count = rela_plt_size // 24
for i in range(entry_count):
    base = rela_plt_off + i * 24
    r_info = struct.unpack_from('<Q', data, base + 8)[0]
    sym_idx = r_info >> 32
    sym_base = dynsym_off + sym_idx * 24
    st_name = struct.unpack_from('<I', data, sym_base)[0]
    name_end = data.find(b'\x00', dynstr_off + st_name)
    name = data[dynstr_off + st_name : name_end].decode('ascii', errors='ignore')
    plt_addr = plt_base + 32 + i * 16
    plt_names[plt_addr] = name

# Disassemble key regions
regions = [
    ("handle_client (cmd_dispatch area)", 0x2200, 0x23f0),
    ("main (socket setup)", 0x2440, 0x26c0),
    ("run_load_ta / cmd processing", 0x2640, 0x2810),
    ("read_file / get_file_size", 0x2808, 0x2a70),
    ("load_app / TrustZone interaction", 0x2a70, 0x2d90),
    ("QSEE command dispatch", 0x2d90, 0x2f80),
    ("magic number / validation", 0x2dc0, 0x2e60),
]

for region_name, start, end in regions:
    print(f"\n--- {region_name} (0x{start:04x}-0x{end:04x}) ---")
    for off in range(start - text_off, end - text_off, 4):
        if off < 0 or off >= len(text_data):
            continue
        insn = struct.unpack_from('<I', text_data, off)[0]
        vaddr = text_off + off
        decoded = decode_insn_basic(insn, vaddr)
        
        # Resolve BL targets to PLT names
        if decoded.startswith("BL 0x"):
            target = int(decoded.split("0x")[1], 16)
            if target in plt_names:
                decoded = f"BL {plt_names[target]}"
        
        marker = ""
        if "CMP" in decoded:
            marker = "  ◄◄◄ CMD_ID CHECK?"
        elif "BLR" in decoded or "BR " in decoded:
            marker = "  ◄◄◄ INDIRECT CALL"
        elif decoded.startswith("BL ") and not decoded.startswith("BL 0x"):
            marker = f"  ◄◄◄"
        elif "RET" in decoded:
            marker = "  ◄◄◄ RETURN"
        
        print(f"  0x{vaddr:08x}: {decoded:50s}{marker}")

# === 3. Analyze the CMP at 0x2e00 (W6, #0xaa) — potential magic number ===
print(f"\n{'='*60}")
print("MAGIC NUMBER CANDIDATE ANALYSIS")
print(f"{'='*60}")

# Look at the context around CMP W6, #0xAA (0x2e00)
print("\nContext around CMP W6, #0xAA at 0x2e00:")
for off in range(0x2dd0 - text_off, 0x2e70 - text_off, 4):
    if off < 0 or off >= len(text_data):
        continue
    insn = struct.unpack_from('<I', text_data, off)[0]
    vaddr = text_off + off
    decoded = decode_insn_basic(insn, vaddr)
    if decoded.startswith("BL 0x"):
        target = int(decoded.split("0x")[1], 16)
        if target in plt_names:
            decoded = f"BL {plt_names[target]}"
    marker = " ***" if vaddr == 0x2e00 else ""
    print(f"  0x{vaddr:08x}: {decoded}{marker}")

# === 4. Find all ADRP+ADD pairs that reference .rodata strings ===
print(f"\n{'='*60}")
print("ALL RODATA REFERENCES FROM .text")
print(f"{'='*60}")

rodata_start = 0x0e18
rodata_end = 0x0e18 + 0xbf2

# Get all strings
rodata_strings = {}
rd = data[rodata_start:rodata_end]
i = 0
while i < len(rd):
    start_i = i
    s = b''
    while i < len(rd) and rd[i] != 0 and 0x20 <= rd[i] < 0x7f:
        s += bytes([rd[i]])
        i += 1
    if len(s) >= 3:
        vaddr = rodata_start + start_i
        rodata_strings[vaddr] = s.decode('ascii')
    i += 1

num_insns = len(text_data) // 4
for i in range(num_insns - 1):
    insn1 = struct.unpack_from('<I', text_data, i*4)[0]
    insn2 = struct.unpack_from('<I', text_data, (i+1)*4)[0]
    vaddr1 = text_off + i * 4
    
    # ADRP
    if (insn1 & 0x9F000000) == 0x90000000:
        rd1 = insn1 & 0x1F
        immhi = (insn1 >> 5) & 0x7FFFF
        immlo = (insn1 >> 29) & 0x3
        imm = (immhi << 2) | immlo
        if imm & 0x100000:
            imm = imm - 0x200000
        page = (vaddr1 & ~0xFFF) + (imm << 12)
        
        # ADD immediate
        if (insn2 & 0x7F800000) == 0x11000000:
            rd2 = insn2 & 0x1F
            rn2 = (insn2 >> 5) & 0x1F
            imm12 = (insn2 >> 10) & 0xFFF
            
            if rn2 == rd1:
                target_addr = page + imm12
                
                if rodata_start <= target_addr < rodata_end:
                    # Find which string this points to
                    for saddr in sorted(rodata_strings.keys()):
                        if saddr <= target_addr < saddr + len(rodata_strings[saddr]) + 1:
                            offset_in_str = target_addr - saddr
                            s = rodata_strings[saddr]
                            if offset_in_str == 0:
                                print(f"  0x{vaddr1:08x}: ADRP+ADD → 0x{target_addr:04x} \"{s[:80]}\"")
                            else:
                                print(f"  0x{vaddr1:08x}: ADRP+ADD → 0x{target_addr:04x} \"{s[offset_in_str:offset_in_str+60]}\" (mid-string, base=\"{s[:30]}\")")
                            break
