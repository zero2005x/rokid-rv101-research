#!/usr/bin/env python3
"""Extract and disassemble run_exec_cmd_ta (0x2a70-0x2e4b) and run_load_ta key parts"""
import struct, sys

BINARY = "vendor_extracted/bin/GateServiced"
with open(BINARY, "rb") as f:
    elf = f.read()

# ELF base: find .text offset. Entry=0x2000, e_entry from ELF header
e_entry = struct.unpack_from("<Q", elf, 24)[0]  # 0x2000
e_phoff = struct.unpack_from("<Q", elf, 32)[0]
e_phentsize = struct.unpack_from("<H", elf, 54)[0]
e_phnum = struct.unpack_from("<H", elf, 56)[0]

# Find PT_LOAD segment containing .text (0x2000)
text_file_offset = None
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p_type = struct.unpack_from("<I", elf, off)[0]
    p_offset = struct.unpack_from("<Q", elf, off+8)[0]
    p_vaddr = struct.unpack_from("<Q", elf, off+16)[0]
    p_filesz = struct.unpack_from("<Q", elf, off+32)[0]
    if p_type == 1 and p_vaddr <= 0x2000 < p_vaddr + p_filesz:
        text_file_offset = p_offset - p_vaddr
        break

if text_file_offset is None:
    print("ERROR: Could not find text segment")
    sys.exit(1)

# Load rodata strings
rodata_off = None
rodata_size = None
# Find .rodata section
e_shoff = struct.unpack_from("<Q", elf, 40)[0]
e_shentsize = struct.unpack_from("<H", elf, 58)[0]
e_shnum = struct.unpack_from("<H", elf, 60)[0]
e_shstrndx = struct.unpack_from("<H", elf, 62)[0]
shstrtab_off = struct.unpack_from("<Q", elf, e_shoff + e_shstrndx * e_shentsize + 24)[0]

for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    sh_name = struct.unpack_from("<I", elf, off)[0]
    name = ""
    j = shstrtab_off + sh_name
    while j < len(elf) and elf[j] != 0:
        name += chr(elf[j])
        j += 1
    if name == ".rodata":
        rodata_off = struct.unpack_from("<Q", elf, off+24)[0]
        rodata_size = struct.unpack_from("<Q", elf, off+32)[0]
        rodata_vaddr = struct.unpack_from("<Q", elf, off+16)[0]
        break

# Build rodata string map
strings = {}
if rodata_off:
    i = 0
    while i < rodata_size:
        if elf[rodata_off + i] >= 0x20 and elf[rodata_off + i] < 0x7f:
            s = ""
            j = i
            while j < rodata_size and elf[rodata_off + j] >= 0x20 and elf[rodata_off + j] < 0x7f:
                s += chr(elf[rodata_off + j])
                j += 1
            if len(s) >= 4:
                strings[rodata_vaddr + i] = s
            i = j + 1
        else:
            i += 1

# PLT symbols (from prior analysis)
plt_syms = {
    0x3680: "__libc_init", 0x3690: "gettimeofday", 0x36a0: "__stack_chk_fail",
    0x36b0: "memset", 0x36c0: "__android_log_print", 0x36d0: "recvfrom",
    0x36e0: "__sendto_chk", 0x36f0: "close", 0x3700: "exit",
    0x3710: "signal", 0x3720: "socket", 0x3730: "setsockopt",
    0x3740: "bind", 0x3750: "listen", 0x3760: "pthread_detach",
    0x3770: "accept", 0x3780: "pthread_create", 0x3790: "__strlen_chk",
    0x37a0: "__strncpy_chk", 0x37b0: "__strncpy_chk2", 0x37c0: "fopen",
    0x37d0: "fseek", 0x37e0: "__errno", 0x37f0: "strerror",
    0x3800: "ftell", 0x3810: "fclose", 0x3820: "fread_unlocked",
    0x3830: "CreateDmabufHeapBufferAllocator", 0x3840: "memcpy",
    0x3850: "dup", 0x3860: "TZCom_getFdObject",
    0x3870: "TZCom_getClientEnvObject", 0x3880: "calloc", 0x3890: "free",
    0x38a0: "DmabufHeapAlloc", 0x38b0: "mmap",
    0x38c0: "DmabufHeapCpuSyncStart", 0x38d0: "munmap",
    0x38e0: "FreeDmabufHeapBufferAllocator", 0x38f0: "DmabufHeapCpuSyncEnd"
}

func_syms = {
    0x2048: "timing_helper", 0x20c0: "handle_client", 0x23f0: "handle_sigint",
    0x2440: "main", 0x263c: "cmd_dispatch", 0x2808: "get_file_size",
    0x2954: "read_file", 0x2a70: "run_exec_cmd_ta", 0x2e4c: "run_load_ta",
    0x3354: "run_unload_ta", 0x346c: "dma_alloc", 0x35d0: "dma_unalloc"
}

def disasm_range(start_vaddr, end_vaddr, label):
    print(f"\n{'='*70}")
    print(f"  {label} (0x{start_vaddr:04x} - 0x{end_vaddr:04x}, {end_vaddr-start_vaddr} bytes)")
    print(f"{'='*70}")
    
    pending_adrp = {}  # reg -> page
    
    for addr in range(start_vaddr, end_vaddr, 4):
        file_off = text_file_offset + addr
        if file_off + 4 > len(elf):
            break
        insn = struct.unpack_from("<I", elf, file_off)[0]
        
        # Decode instruction
        comment = ""
        mnemonic = f"({insn:08x})"
        
        # STP pre-index (frame setup)
        if (insn & 0xFFC00000) == 0xA9800000 or (insn & 0xFFC00000) == 0xA9000000:
            rt = insn & 0x1f
            rt2 = (insn >> 10) & 0x1f
            rn = (insn >> 5) & 0x1f
            imm7 = (insn >> 15) & 0x7f
            if imm7 & 0x40: imm7 -= 128
            mnemonic = f"STP X{rt}, X{rt2}, [X{rn}, #{imm7*8}]"
            if (insn & 0xFFC00000) == 0xA9800000:
                mnemonic += "!"
        
        # SUB SP immediate
        elif (insn & 0xFF000000) == 0xD1000000:
            rd = insn & 0x1f
            rn = (insn >> 5) & 0x1f
            imm12 = (insn >> 10) & 0xfff
            shift = (insn >> 22) & 0x3
            val = imm12 << (12 if shift else 0)
            if rd == 31 and rn == 31:
                mnemonic = f"SUB SP, SP, #0x{val:x}"
                comment = f"frame={val}"
        
        # ADD immediate
        elif (insn & 0xFF000000) == 0x91000000:
            rd = insn & 0x1f
            rn = (insn >> 5) & 0x1f
            imm12 = (insn >> 10) & 0xfff
            shift = (insn >> 22) & 0x3
            val = imm12 << (12 if shift else 0)
            rn_name = f"X{rn}" if rn != 31 else "SP"
            rd_name = f"X{rd}" if rd != 31 else "SP"
            mnemonic = f"ADD {rd_name}, {rn_name}, #0x{val:x}"
            if rn in pending_adrp and shift == 0:
                resolved = pending_adrp[rn] + val
                if resolved in strings:
                    s = strings[resolved][:60]
                    comment = f'"{s}"'
                elif resolved in func_syms:
                    comment = func_syms[resolved]
                else:
                    comment = f"0x{resolved:x}"
        
        # ADRP
        elif (insn & 0x9F000000) == 0x90000000:
            rd = insn & 0x1f
            immhi = (insn >> 5) & 0x7ffff
            immlo = (insn >> 29) & 0x3
            imm = (immhi << 2) | immlo
            if imm & 0x100000: imm -= 0x200000
            page = (addr & ~0xfff) + (imm << 12)
            pending_adrp[rd] = page
            mnemonic = f"ADRP X{rd}, 0x{page:x}"
        
        # MOV wide immediate
        elif (insn & 0xFF800000) == 0x52800000:
            rd = insn & 0x1f
            imm16 = (insn >> 5) & 0xffff
            mnemonic = f"MOV W{rd}, #0x{imm16:x}"
            if imm16 >= 0x3000 and imm16 <= 0x3fff:
                comment = "QSEE error code"
        elif (insn & 0xFF800000) == 0xD2800000:
            rd = insn & 0x1f
            imm16 = (insn >> 5) & 0xffff
            mnemonic = f"MOV X{rd}, #0x{imm16:x}"
        
        # MOVK
        elif (insn & 0xFF800000) == 0x72A00000:
            rd = insn & 0x1f
            imm16 = (insn >> 5) & 0xffff
            mnemonic = f"MOVK W{rd}, #0x{imm16:x}, LSL #16"
        elif (insn & 0xFF800000) == 0xF2A00000:
            rd = insn & 0x1f
            imm16 = (insn >> 5) & 0xffff
            mnemonic = f"MOVK X{rd}, #0x{imm16:x}, LSL #16"
        elif (insn & 0xFF800000) == 0xF2C00000:
            rd = insn & 0x1f
            imm16 = (insn >> 5) & 0xffff
            mnemonic = f"MOVK X{rd}, #0x{imm16:x}, LSL #32"
        elif (insn & 0xFF800000) == 0xF2E00000:
            rd = insn & 0x1f
            imm16 = (insn >> 5) & 0xffff
            mnemonic = f"MOVK X{rd}, #0x{imm16:x}, LSL #48"
        
        # BL (branch with link)
        elif (insn & 0xFC000000) == 0x94000000:
            imm26 = insn & 0x3ffffff
            if imm26 & 0x2000000: imm26 -= 0x4000000
            target = addr + imm26 * 4
            name = plt_syms.get(target, func_syms.get(target, f"0x{target:04x}"))
            mnemonic = f"BL {name}"
            comment = ""
        
        # BLR (branch register)
        elif (insn & 0xFFFFFC1F) == 0xD63F0000:
            rn = (insn >> 5) & 0x1f
            mnemonic = f"BLR X{rn}"
            comment = "indirect call (vtable)"
        
        # B (unconditional branch)
        elif (insn & 0xFC000000) == 0x14000000:
            imm26 = insn & 0x3ffffff
            if imm26 & 0x2000000: imm26 -= 0x4000000
            target = addr + imm26 * 4
            mnemonic = f"B 0x{target:04x}"
        
        # B.cond
        elif (insn & 0xFF000010) == 0x54000000:
            cond = insn & 0xf
            imm19 = (insn >> 5) & 0x7ffff
            if imm19 & 0x40000: imm19 -= 0x80000
            target = addr + imm19 * 4
            conds = {0:"EQ",1:"NE",2:"CS/HS",3:"CC/LO",4:"MI",5:"PL",6:"VS",7:"VC",8:"HI",9:"LS",10:"GE",11:"LT",12:"GT",13:"LE",14:"AL"}
            cname = conds.get(cond, f"?{cond}")
            mnemonic = f"B.{cname} 0x{target:04x}"
        
        # CBZ/CBNZ
        elif (insn & 0x7F000000) == 0x34000000:
            sf = (insn >> 31) & 1
            op = (insn >> 24) & 1
            imm19 = (insn >> 5) & 0x7ffff
            if imm19 & 0x40000: imm19 -= 0x80000
            target = addr + imm19 * 4
            rt = insn & 0x1f
            rname = f"X{rt}" if sf else f"W{rt}"
            mnemonic = f"{'CBNZ' if op else 'CBZ'} {rname}, 0x{target:04x}"
        
        # CMP immediate (SUB with Rd=XZR)
        elif (insn & 0xFF000000) == 0xF100001F or (insn & 0x7F00001F) == 0x7100001F:
            sf = (insn >> 31) & 1
            rn = (insn >> 5) & 0x1f
            imm12 = (insn >> 10) & 0xfff
            rname = f"X{rn}" if sf else f"W{rn}"
            mnemonic = f"CMP {rname}, #0x{imm12:x}"
            comment = f"({imm12})"
        elif (insn & 0xFFE0001F) == 0xEB00001F or (insn & 0x7FE0001F) == 0x6B00001F:
            sf = (insn >> 31) & 1
            rn = (insn >> 5) & 0x1f
            rm = (insn >> 16) & 0x1f
            rp = "X" if sf else "W"
            mnemonic = f"CMP {rp}{rn}, {rp}{rm}"
        
        # STR immediate
        elif (insn & 0xFFC00000) == 0xF9000000:
            rt = insn & 0x1f
            rn = (insn >> 5) & 0x1f
            imm12 = (insn >> 10) & 0xfff
            mnemonic = f"STR X{rt}, [X{rn}, #0x{imm12*8:x}]"
        # STRB
        elif (insn & 0xFFC00000) == 0x39000000:
            rt = insn & 0x1f
            rn = (insn >> 5) & 0x1f
            imm12 = (insn >> 10) & 0xfff
            mnemonic = f"STRB W{rt}, [X{rn}, #0x{imm12:x}]"
        
        # LDR immediate
        elif (insn & 0xFFC00000) == 0xF9400000:
            rt = insn & 0x1f
            rn = (insn >> 5) & 0x1f
            imm12 = (insn >> 10) & 0xfff
            mnemonic = f"LDR X{rt}, [X{rn}, #0x{imm12*8:x}]"
        # LDRB
        elif (insn & 0xFFC00000) == 0x39400000:
            rt = insn & 0x1f
            rn = (insn >> 5) & 0x1f
            imm12 = (insn >> 10) & 0xfff
            mnemonic = f"LDRB W{rt}, [X{rn}, #0x{imm12:x}]"
        # LDR W
        elif (insn & 0xFFC00000) == 0xB9400000:
            rt = insn & 0x1f
            rn = (insn >> 5) & 0x1f
            imm12 = (insn >> 10) & 0xfff
            mnemonic = f"LDR W{rt}, [X{rn}, #0x{imm12*4:x}]"
        
        # MOV register (ORR Xd, XZR, Xm)
        elif (insn & 0xFFE0FFE0) == 0xAA0003E0:
            rd = insn & 0x1f
            rm = (insn >> 16) & 0x1f
            mnemonic = f"MOV X{rd}, X{rm}"
        elif (insn & 0xFFE0FFE0) == 0x2A0003E0:
            rd = insn & 0x1f
            rm = (insn >> 16) & 0x1f
            mnemonic = f"MOV W{rd}, W{rm}"
        
        # RET
        elif insn == 0xD65F03C0:
            mnemonic = "RET"
        
        # NOP
        elif insn == 0xD503201F:
            mnemonic = "NOP"
        
        line = f"  {addr:08x}: {mnemonic:55s}"
        if comment:
            line += f"  // {comment}"
        print(line)

# Disassemble run_exec_cmd_ta
disasm_range(0x2a70, 0x2e4c, "run_exec_cmd_ta")

# Disassemble run_load_ta key areas (just the file read portion: 0x2f80-0x3050)
disasm_range(0x2f80, 0x3100, "run_load_ta (file read + TZ load section)")

# Disassemble dma_alloc
disasm_range(0x346c, 0x35d0, "dma_alloc")

# Disassemble get_file_size
disasm_range(0x2808, 0x2954, "get_file_size")
