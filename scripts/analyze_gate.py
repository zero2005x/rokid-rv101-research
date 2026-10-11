#!/usr/bin/env python3
"""Deep static analysis of GateServiced binary - ELF parsing, symbol extraction, code analysis"""

import struct
import sys

def read_elf(path):
    with open(path, 'rb') as f:
        return f.read()

def parse_elf64(data):
    """Parse ELF64 header and extract key sections"""
    info = {}
    
    # ELF header
    info['ei_class'] = data[4]  # 2 = 64-bit
    info['ei_data'] = data[5]   # 1 = LE
    info['e_type'] = struct.unpack_from('<H', data, 16)[0]
    info['e_machine'] = struct.unpack_from('<H', data, 18)[0]
    info['e_entry'] = struct.unpack_from('<Q', data, 24)[0]
    info['e_phoff'] = struct.unpack_from('<Q', data, 32)[0]
    info['e_shoff'] = struct.unpack_from('<Q', data, 40)[0]
    info['e_phentsize'] = struct.unpack_from('<H', data, 54)[0]
    info['e_phnum'] = struct.unpack_from('<H', data, 56)[0]
    info['e_shentsize'] = struct.unpack_from('<H', data, 58)[0]
    info['e_shnum'] = struct.unpack_from('<H', data, 60)[0]
    info['e_shstrndx'] = struct.unpack_from('<H', data, 62)[0]
    
    return info

def get_sections(data, info):
    """Parse section headers"""
    sections = []
    shstrtab_base = info['e_shoff'] + info['e_shstrndx'] * info['e_shentsize']
    shstrtab_offset = struct.unpack_from('<Q', data, shstrtab_base + 24)[0]
    
    for i in range(info['e_shnum']):
        base = info['e_shoff'] + i * info['e_shentsize']
        sh_name_idx = struct.unpack_from('<I', data, base)[0]
        sh_type = struct.unpack_from('<I', data, base + 4)[0]
        sh_flags = struct.unpack_from('<Q', data, base + 8)[0]
        sh_addr = struct.unpack_from('<Q', data, base + 16)[0]
        sh_offset = struct.unpack_from('<Q', data, base + 24)[0]
        sh_size = struct.unpack_from('<Q', data, base + 32)[0]
        sh_link = struct.unpack_from('<I', data, base + 40)[0]
        sh_info = struct.unpack_from('<I', data, base + 44)[0]
        
        name_end = data.find(b'\x00', shstrtab_offset + sh_name_idx)
        name = data[shstrtab_offset + sh_name_idx : name_end].decode('ascii', errors='ignore')
        
        sections.append({
            'name': name, 'type': sh_type, 'flags': sh_flags,
            'addr': sh_addr, 'offset': sh_offset, 'size': sh_size,
            'link': sh_link, 'info': sh_info
        })
    return sections

def get_dynsym(data, sections):
    """Parse dynamic symbol table"""
    dynsym = None
    dynstr = None
    for s in sections:
        if s['name'] == '.dynsym':
            dynsym = s
        if s['name'] == '.dynstr':
            dynstr = s
    
    if not dynsym or not dynstr:
        return []
    
    symbols = []
    entry_size = 24  # Elf64_Sym
    count = dynsym['size'] // entry_size
    
    for i in range(count):
        base = dynsym['offset'] + i * entry_size
        st_name = struct.unpack_from('<I', data, base)[0]
        st_info = data[base + 4]
        st_other = data[base + 5]
        st_shndx = struct.unpack_from('<H', data, base + 6)[0]
        st_value = struct.unpack_from('<Q', data, base + 8)[0]
        st_size = struct.unpack_from('<Q', data, base + 16)[0]
        
        name_end = data.find(b'\x00', dynstr['offset'] + st_name)
        name = data[dynstr['offset'] + st_name : name_end].decode('ascii', errors='ignore')
        
        bind = st_info >> 4
        stype = st_info & 0xf
        
        symbols.append({
            'name': name, 'value': st_value, 'size': st_size,
            'bind': bind, 'type': stype, 'shndx': st_shndx
        })
    return symbols

def get_rodata_strings(data, sections):
    """Extract strings from .rodata with virtual addresses"""
    rodata = None
    for s in sections:
        if s['name'] == '.rodata':
            rodata = s
            break
    
    if not rodata:
        return []
    
    strings = []
    i = rodata['offset']
    end = rodata['offset'] + rodata['size']
    
    while i < end:
        start = i
        s = b''
        while i < end and data[i] != 0:
            if 0x20 <= data[i] < 0x7f:
                s += bytes([data[i]])
            else:
                s = b''  # non-printable, reset
                break
            i += 1
        if len(s) >= 3 and i < end and data[i] == 0:
            vaddr = rodata['addr'] + (start - rodata['offset'])
            strings.append((vaddr, s.decode('ascii')))
        i += 1
    
    return strings

def get_rela_plt(data, sections):
    """Parse .rela.plt for PLT entries (imported functions)"""
    rela_plt = None
    dynsym_sec = None
    dynstr_sec = None
    
    for s in sections:
        if s['name'] == '.rela.plt':
            rela_plt = s
        if s['name'] == '.dynsym':
            dynsym_sec = s
        if s['name'] == '.dynstr':
            dynstr_sec = s
    
    if not rela_plt or not dynsym_sec or not dynstr_sec:
        return []
    
    entries = []
    entry_size = 24  # Elf64_Rela
    count = rela_plt['size'] // entry_size
    
    for i in range(count):
        base = rela_plt['offset'] + i * entry_size
        r_offset = struct.unpack_from('<Q', data, base)[0]
        r_info = struct.unpack_from('<Q', data, base + 8)[0]
        r_addend = struct.unpack_from('<q', data, base + 16)[0]
        
        sym_idx = r_info >> 32
        rel_type = r_info & 0xffffffff
        
        # Get symbol name
        sym_base = dynsym_sec['offset'] + sym_idx * 24
        st_name = struct.unpack_from('<I', data, sym_base)[0]
        name_end = data.find(b'\x00', dynstr_sec['offset'] + st_name)
        name = data[dynstr_sec['offset'] + st_name : name_end].decode('ascii', errors='ignore')
        
        entries.append({
            'offset': r_offset, 'sym_idx': sym_idx,
            'type': rel_type, 'name': name, 'addend': r_addend
        })
    
    return entries

def analyze_text_section(data, sections):
    """Basic analysis of .text section - find AArch64 instruction patterns"""
    text = None
    for s in sections:
        if s['name'] == '.text':
            text = s
            break
    
    if not text:
        return
    
    print(f"\n{'='*60}")
    print(f".text section: vaddr=0x{text['addr']:08x}, size=0x{text['size']:x} ({text['size']} bytes)")
    print(f"{'='*60}")
    
    # AArch64 instructions are 4 bytes each
    text_data = data[text['offset']:text['offset']+text['size']]
    num_insns = len(text_data) // 4
    print(f"Total instructions: {num_insns}")
    
    # Search for interesting patterns in AArch64
    # BL (Branch with Link) = 0x94000000 mask 0xFC000000
    # ADRP = 0x90000000 mask 0x9F000000
    # SVC (syscall) = 0xD4000001 mask 0xFFE0001F
    # CMP immediate = 0x7100001F mask 0x7F80001F (W reg) or 0xF100001F (X reg)
    
    bl_count = 0
    svc_count = 0
    cmp_imm_values = []
    
    for i in range(num_insns):
        insn = struct.unpack_from('<I', text_data, i*4)[0]
        vaddr = text['addr'] + i * 4
        
        # BL instruction
        if (insn & 0xFC000000) == 0x94000000:
            offset = insn & 0x03FFFFFF
            if offset & 0x02000000:  # sign extend
                offset |= 0xFC000000
                offset = offset - 0x100000000
            target = vaddr + offset * 4
            bl_count += 1
        
        # SVC (system call)
        if (insn & 0xFFE0001F) == 0xD4000001:
            svc_num = (insn >> 5) & 0xFFFF
            svc_count += 1
            print(f"  SVC #{svc_num} at 0x{vaddr:08x}")
        
        # CMP Wn, #imm (looking for cmd_id comparisons)
        if (insn & 0x7F80001F) == 0x7100001F:
            rd = insn & 0x1F  # should be 0x1F for CMP (alias of SUBS with WZR)
            imm12 = (insn >> 10) & 0xFFF
            shift = (insn >> 22) & 0x3
            rn = (insn >> 5) & 0x1F
            if shift == 1:
                imm12 <<= 12
            cmp_imm_values.append((vaddr, rn, imm12))
    
    print(f"\nBL (function call) count: {bl_count}")
    print(f"SVC (syscall) count: {svc_count}")
    
    print(f"\n=== CMP Wn, #imm instructions (potential cmd_id checks) ===")
    for vaddr, rn, imm in cmp_imm_values:
        print(f"  0x{vaddr:08x}: CMP W{rn}, #0x{imm:x} ({imm})")
    
    # Search for TBZ/TBNZ (test bit and branch) - common in switch tables
    print(f"\n=== Switch-related patterns ===")
    for i in range(num_insns):
        insn = struct.unpack_from('<I', text_data, i*4)[0]
        vaddr = text['addr'] + i * 4
        
        # CBZ/CBNZ
        if (insn & 0x7E000000) == 0x34000000:
            op = (insn >> 24) & 1
            rt = insn & 0x1F
            imm19 = (insn >> 5) & 0x7FFFF
            if imm19 & 0x40000:
                imm19 = imm19 - 0x80000
            target = vaddr + imm19 * 4
            op_name = "CBNZ" if op else "CBZ"
            # Only show ones near interesting areas
        
        # BR Xn (indirect branch - jump table)
        if (insn & 0xFFFFFC1F) == 0xD61F0000:
            rn = (insn >> 5) & 0x1F
            print(f"  0x{vaddr:08x}: BR X{rn} (indirect branch - possible jump table!)")
        
        # BLR Xn (indirect call)
        if (insn & 0xFFFFFC1F) == 0xD63F0000:
            rn = (insn >> 5) & 0x1F
            print(f"  0x{vaddr:08x}: BLR X{rn} (indirect call)")

def analyze_got_plt(data, sections):
    """Analyze GOT/PLT for imported function resolution order"""
    plt = None
    got_plt = None
    for s in sections:
        if s['name'] == '.plt':
            plt = s
        if s['name'] == '.got.plt':
            got_plt = s
    
    if plt:
        plt_entries = (plt['size'] - 32) // 16  # First 32 bytes = PLT[0] stub
        print(f"\n.plt: {plt_entries} entries (each function import)")
        print(f"  PLT base: 0x{plt['addr']:08x}")
        for i in range(plt_entries):
            entry_addr = plt['addr'] + 32 + i * 16
            print(f"  PLT[{i+1}]: 0x{entry_addr:08x}")

def find_magic_numbers(data, sections):
    """Search for potential magic number constants in .rodata and .text"""
    rodata = None
    for s in sections:
        if s['name'] == '.rodata':
            rodata = s
            break
    
    print(f"\n{'='*60}")
    print(f"MAGIC NUMBER SEARCH")
    print(f"{'='*60}")
    
    # Search .rodata for 4-byte aligned potential magic values
    if rodata:
        rd = data[rodata['offset']:rodata['offset']+rodata['size']]
        print("\n4-byte constants in .rodata (non-string, non-zero):")
        i = 0
        while i < len(rd) - 3:
            # Check if this looks like a magic number (not a string)
            val = struct.unpack_from('<I', rd, i)[0]
            if val != 0 and not all(0x20 <= b < 0x7f for b in rd[i:i+4]):
                # Check if it's surrounded by null bytes or string data
                if i > 0 and rd[i-1] == 0:
                    vaddr = rodata['addr'] + i
                    print(f"  0x{vaddr:04x}: 0x{val:08x} ({val})")
            i += 4

def analyze_program_flow(data, sections):
    """Analyze .text for function prologue/epilogue patterns to identify functions"""
    text = None
    for s in sections:
        if s['name'] == '.text':
            text = s
            break
    
    if not text:
        return
    
    text_data = data[text['offset']:text['offset']+text['size']]
    num_insns = len(text_data) // 4
    
    print(f"\n{'='*60}")
    print(f"FUNCTION IDENTIFICATION (AArch64 prologue detection)")
    print(f"{'='*60}")
    
    functions = []
    for i in range(num_insns - 1):
        insn = struct.unpack_from('<I', text_data, i*4)[0]
        next_insn = struct.unpack_from('<I', text_data, (i+1)*4)[0]
        vaddr = text['addr'] + i * 4
        
        # Common AArch64 function prologue:
        # STP X29, X30, [SP, #-imm]! (save FP and LR)
        # Pattern: 0xA9xx7BFD where xx varies
        if (insn & 0xFFE07FFF) == 0xA9007BFD:
            # This is STP x29, x30, [sp, #off]!
            functions.append(vaddr)
        # Alternative: SUB SP, SP, #imm followed by STP
        elif (insn & 0xFF0003FF) == 0xD10003FF:  # SUB SP, SP, #imm
            imm = (insn >> 10) & 0xFFF
            if (next_insn & 0x7FC07FFF) == 0x29007BFD:  # STP x29,x30,[sp,off]
                functions.append(vaddr)
    
    print(f"\nDetected {len(functions)} function prologues:")
    for addr in functions:
        print(f"  func_0x{addr:08x}")
    
    return functions

def find_string_xrefs(data, sections, target_string):
    """Find cross-references to a string in .rodata from .text"""
    rodata = None
    text = None
    for s in sections:
        if s['name'] == '.rodata':
            rodata = s
        if s['name'] == '.text':
            text = s
    
    if not rodata or not text:
        return []
    
    # Find string in rodata
    rodata_data = data[rodata['offset']:rodata['offset']+rodata['size']]
    str_bytes = target_string.encode('ascii')
    pos = rodata_data.find(str_bytes)
    if pos < 0:
        return []
    
    str_vaddr = rodata['addr'] + pos
    str_page = str_vaddr & ~0xFFF  # page aligned
    str_pageoff = str_vaddr & 0xFFF
    
    # Search .text for ADRP+ADD pairs that reference this address
    text_data = data[text['offset']:text['offset']+text['size']]
    num_insns = len(text_data) // 4
    
    xrefs = []
    for i in range(num_insns - 1):
        insn = struct.unpack_from('<I', text_data, i*4)[0]
        vaddr = text['addr'] + i * 4
        
        # ADRP Xd, page
        if (insn & 0x9F000000) == 0x90000000:
            rd = insn & 0x1F
            immhi = (insn >> 5) & 0x7FFFF
            immlo = (insn >> 29) & 0x3
            imm = (immhi << 2) | immlo
            if imm & 0x100000:  # sign extend 21-bit
                imm = imm - 0x200000
            page = (vaddr & ~0xFFF) + (imm << 12)
            
            if page == str_page:
                # Check next instruction for ADD with pageoff
                next_insn = struct.unpack_from('<I', text_data, (i+1)*4)[0]
                # ADD Xd, Xn, #pageoff
                if (next_insn & 0xFFC00000) == 0x91000000:
                    add_imm = (next_insn >> 10) & 0xFFF
                    add_rn = (next_insn >> 5) & 0x1F
                    if add_rn == rd and add_imm == str_pageoff:
                        xrefs.append(vaddr)
    
    return xrefs


def main():
    path = r'vendor_extracted\bin\GateServiced'
    data = read_elf(path)
    
    print(f"{'='*60}")
    print(f"GateServiced Deep Binary Analysis")
    print(f"File: {path} ({len(data)} bytes)")
    print(f"{'='*60}")
    
    info = parse_elf64(data)
    sections = get_sections(data, info)
    
    print(f"\nEntry point: 0x{info['e_entry']:08x}")
    print(f"Type: {'DYN (PIE)' if info['e_type']==3 else 'EXEC'}")
    
    # 1. Dynamic symbols
    print(f"\n{'='*60}")
    print(f"DYNAMIC SYMBOLS")
    print(f"{'='*60}")
    
    symbols = get_dynsym(data, sections)
    
    imported = [s for s in symbols if s['shndx'] == 0 and s['name']]
    exported = [s for s in symbols if s['shndx'] != 0 and s['name']]
    
    print(f"\n--- Imported functions ({len(imported)}) ---")
    for s in imported:
        bind_s = {0:'LOCAL',1:'GLOBAL',2:'WEAK'}.get(s['bind'], '?')
        type_s = {0:'NOTYPE',1:'OBJ',2:'FUNC'}.get(s['type'], '?')
        print(f"  {type_s:5s} {s['name']}")
    
    print(f"\n--- Exported symbols ({len(exported)}) ---")
    for s in exported:
        bind_s = {0:'LOCAL',1:'GLOBAL',2:'WEAK'}.get(s['bind'], '?')
        type_s = {0:'NOTYPE',1:'OBJ',2:'FUNC'}.get(s['type'], '?')
        print(f"  {type_s:5s} 0x{s['value']:08x} sz={s['size']:4d}  {s['name']}")
    
    # 2. PLT entries (imported function call order)
    print(f"\n{'='*60}")
    print(f"PLT RELOCATIONS (imported function call stubs)")
    print(f"{'='*60}")
    
    rela = get_rela_plt(data, sections)
    for i, r in enumerate(rela):
        print(f"  PLT[{i+1:2d}] → {r['name']:40s} GOT@0x{r['offset']:08x}")
    
    # 3. All strings in .rodata
    print(f"\n{'='*60}")
    print(f".rodata STRINGS")
    print(f"{'='*60}")
    
    strings = get_rodata_strings(data, sections)
    for vaddr, s in strings:
        print(f"  0x{vaddr:04x}: \"{s}\"")
    
    # 4. String cross-references for critical functions
    print(f"\n{'='*60}")
    print(f"STRING CROSS-REFERENCES (.text → .rodata)")
    print(f"{'='*60}")
    
    critical_strings = [
        "cmd_id = %x",
        "Failed to open file",
        "Load app %s failed",
        "received magicNumTA failed",
        "data_len exceeds maximum length",
        "wait for client data",
        "ta version",
        "unload ta",
        "run_exec_cmd_ta",
        "dma_alloc failed",
        "Mapping sharedMemObj",
    ]
    
    for target in critical_strings:
        xrefs = find_string_xrefs(data, sections, target)
        if xrefs:
            print(f"\n  \"{target}\"")
            for xref in xrefs:
                print(f"    → referenced from .text 0x{xref:08x}")
        else:
            # Try partial match
            found = False
            for vaddr, s in strings:
                if target.lower() in s.lower():
                    xrefs2 = find_string_xrefs(data, sections, s[:20])
                    if xrefs2:
                        print(f"\n  \"{s[:60]}\"")
                        for xref in xrefs2:
                            print(f"    → referenced from .text 0x{xref:08x}")
                        found = True
                        break
            if not found:
                print(f"\n  \"{target}\" — no xref found (may use different addressing)")
    
    # 5. Analyze .text for patterns
    analyze_text_section(data, sections)
    
    # 6. Function identification
    functions = analyze_program_flow(data, sections)
    
    # 7. Magic number search
    find_magic_numbers(data, sections)
    
    # 8. PLT call analysis — which imported functions are called and from where
    print(f"\n{'='*60}")
    print(f"PLT CALL ANALYSIS (BL to .plt entries)")
    print(f"{'='*60}")
    
    text_sec = None
    plt_sec = None
    for s in sections:
        if s['name'] == '.text':
            text_sec = s
        if s['name'] == '.plt':
            plt_sec = s
    
    if text_sec and plt_sec:
        text_data = data[text_sec['offset']:text_sec['offset']+text_sec['size']]
        num_insns = len(text_data) // 4
        
        # Build PLT entry to name mapping
        plt_map = {}
        for i, r in enumerate(rela):
            entry_addr = plt_sec['addr'] + 32 + i * 16
            plt_map[entry_addr] = r['name']
        
        for i in range(num_insns):
            insn = struct.unpack_from('<I', text_data, i*4)[0]
            vaddr = text_sec['addr'] + i * 4
            
            # BL imm26
            if (insn & 0xFC000000) == 0x94000000:
                offset26 = insn & 0x03FFFFFF
                if offset26 & 0x02000000:
                    offset26 |= 0xFC000000
                    offset26 = offset26 - 0x100000000
                target = vaddr + offset26 * 4
                
                if target in plt_map:
                    print(f"  0x{vaddr:08x}: BL {plt_map[target]}")
    
    # 9. Hex dump of .gnu_debugdata (compressed debug info)
    print(f"\n{'='*60}")
    print(f".gnu_debugdata section (compressed mini debug info)")
    print(f"{'='*60}")
    for s in sections:
        if s['name'] == '.gnu_debugdata':
            debug_data = data[s['offset']:s['offset']+min(s['size'], 32)]
            print(f"  Size: {s['size']} bytes")
            print(f"  First 32 bytes: {debug_data.hex()}")
            # Check if XZ compressed (magic: FD 37 7A 58 5A 00)
            if debug_data[:6] == b'\xfd7zXZ\x00':
                print(f"  Format: XZ compressed (can decompress with lzma module)")
            break

if __name__ == '__main__':
    main()
