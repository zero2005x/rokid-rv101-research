#!/usr/bin/env python3
"""
GateServiced Protocol Validation & Testing Framework
=====================================================
Phase 2: Dynamic on-device testing based on Phase 1 RE analysis.

PROTOCOL:
  Request:  [cmd_id:4B LE][payload:variable]
  Response: [data:variable]

COMMANDS:
  0x00010003 = LOAD_TA    (payload = TA file path, max 128 bytes)
  0x00010004 = UNLOAD_TA  (no payload)
  <other>    = EXEC_CMD   (payload = TA command data, forwarded to loaded TA)

ERROR CODES (returned as int, NOT in TCP response directly):
  0x3001 = GENERAL_ERROR (file not found/empty)
  0x3002 = INVALID_PARAMETER (ta_name > 128 bytes)
  0x3003 = NOT_SUPPORTED (read_file failed)
  0x3004 = ALLOC_FAILED (DMA alloc fail)
  0x3005 = DMA_ALLOC_FAILED
  0x300B = INVALID_APP (NULL path)
  0x300C = TA_NOT_LOADED
  0x300D = TA_DEAD (appObj NULL)
  0x300E = RESP_INVALID (zero-length response)

TARGET: TCP 127.0.0.1:8341 (GateServiced, must run on device via adb)

Usage:
  adb push gate_protocol_test.py /data/local/tmp/
  adb shell python3 /data/local/tmp/gate_protocol_test.py [test_name]

  Or from host via adb forward:
  adb forward tcp:8341 tcp:8341
  python3 gate_protocol_test.py --host 127.0.0.1 [test_name]
"""

import socket
import struct
import sys
import time
import os

# ─── Configuration ───
HOST = "127.0.0.1"
PORT = 8341
TIMEOUT = 5
VERBOSE = True

# ─── Protocol Constants ───
CMD_LOAD_TA   = 0x00010003
CMD_UNLOAD_TA = 0x00010004
MAX_TA_NAME   = 128
MAX_FRAME     = 0x5000  # 20480 bytes

# ─── Colors (ANSI) ───
RED    = "\033[91m"
GREEN  = "\033[92m"
YELLOW = "\033[93m"
CYAN   = "\033[96m"
RESET  = "\033[0m"
BOLD   = "\033[1m"

def log(msg, color=""):
    if VERBOSE:
        print(f"{color}{msg}{RESET}")

def log_ok(msg):    log(f"[+] {msg}", GREEN)
def log_err(msg):   log(f"[-] {msg}", RED)
def log_info(msg):  log(f"[*] {msg}", CYAN)
def log_warn(msg):  log(f"[!] {msg}", YELLOW)

# ─── Protocol Layer ───

def connect():
    """Connect to GateServiced TCP socket"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(TIMEOUT)
    try:
        sock.connect((HOST, PORT))
        return sock
    except Exception as e:
        log_err(f"Connection failed: {e}")
        return None

def send_cmd(sock, cmd_id, payload=b""):
    """Send a command frame: [cmd_id:4B LE][payload]"""
    frame = struct.pack("<I", cmd_id) + payload
    if len(frame) > MAX_FRAME:
        log_warn(f"Frame size {len(frame)} exceeds max {MAX_FRAME}, truncating")
        frame = frame[:MAX_FRAME]
    
    log_info(f"TX: cmd_id=0x{cmd_id:08X} payload_len={len(payload)} total={len(frame)}")
    if payload and len(payload) <= 256:
        log_info(f"    payload={payload[:64]!r}{'...' if len(payload) > 64 else ''}")
    
    sock.sendall(frame)

def recv_response(sock, max_size=MAX_FRAME):
    """Receive response data"""
    try:
        data = sock.recv(max_size)
        if data:
            log_ok(f"RX: {len(data)} bytes")
            if len(data) <= 256:
                # Try to decode as ASCII
                try:
                    text = data.decode('ascii', errors='replace')
                    log_ok(f"    text: {text!r}")
                except:
                    pass
                log_ok(f"    hex: {data.hex()}")
            else:
                log_ok(f"    hex (first 64B): {data[:64].hex()}")
        else:
            log_warn("RX: 0 bytes (connection closed)")
        return data
    except socket.timeout:
        log_warn("RX: timeout")
        return None

def send_and_recv(cmd_id, payload=b""):
    """One-shot: connect, send command, receive response, close"""
    sock = connect()
    if not sock:
        return None
    try:
        send_cmd(sock, cmd_id, payload)
        response = recv_response(sock)
        return response
    except Exception as e:
        log_err(f"Error: {e}")
        return None
    finally:
        sock.close()

# ─── Test Functions ───

def test_1_connectivity():
    """Test 1: Basic TCP connectivity to port 8341"""
    print(f"\n{BOLD}{'='*60}")
    print(f"  TEST 1: TCP Connectivity")
    print(f"{'='*60}{RESET}\n")
    
    sock = connect()
    if sock:
        log_ok(f"Connected to {HOST}:{PORT}")
        # Send minimal data (just 1 byte — should be rejected as too short)
        sock.send(b"\x00")
        time.sleep(0.5)
        try:
            data = sock.recv(1024)
            log_info(f"Response to 1-byte packet: {data!r}")
        except socket.timeout:
            log_info("No response to 1-byte packet (expected — too short)")
        sock.close()
        return True
    else:
        log_err("Cannot connect to GateServiced")
        return False

def test_2_protocol_format():
    """Test 2: Validate protocol format with known cmd_ids"""
    print(f"\n{BOLD}{'='*60}")
    print(f"  TEST 2: Protocol Format Validation")
    print(f"{'='*60}{RESET}\n")
    
    results = {}
    
    # Test 2a: UNLOAD_TA (0x10004) — should work even if no TA loaded
    log_info("Test 2a: UNLOAD_TA (0x10004) — no payload")
    resp = send_and_recv(CMD_UNLOAD_TA)
    results['unload'] = resp
    if resp and b"unload ta succeed" in resp:
        log_ok("UNLOAD_TA response confirmed: protocol format VALID")
    elif resp:
        log_warn(f"Unexpected UNLOAD response: {resp!r}")
    time.sleep(0.3)
    
    # Test 2b: Short payload (2 bytes) — should be rejected
    log_info("\nTest 2b: Too-short frame (2 bytes)")
    resp = send_and_recv(0x0000, b"")  # cmd_id=0 but only 4 bytes total
    # Actually this is 4 bytes (just cmd_id), which IS >= 4, so it passes the length check
    results['short'] = resp
    time.sleep(0.3)
    
    # Test 2c: LOAD_TA with empty path
    log_info("\nTest 2c: LOAD_TA (0x10003) with empty payload")
    resp = send_and_recv(CMD_LOAD_TA, b"")
    results['load_empty'] = resp
    time.sleep(0.3)
    
    # Test 2d: LOAD_TA with a known-nonexistent path
    log_info("\nTest 2d: LOAD_TA with nonexistent path")
    resp = send_and_recv(CMD_LOAD_TA, b"/nonexistent/path/test.mbn\x00")
    results['load_noexist'] = resp
    time.sleep(0.3)
    
    # Test 2e: EXEC_CMD with random cmd_id (no TA loaded)
    log_info("\nTest 2e: EXEC_CMD (0x12345678) — no TA loaded")
    resp = send_and_recv(0x12345678, b"\x00" * 16)
    results['exec_no_ta'] = resp
    time.sleep(0.3)
    
    return results

def test_3_boundary_checks():
    """Test 3: Boundary condition testing"""
    print(f"\n{BOLD}{'='*60}")
    print(f"  TEST 3: Boundary Conditions")
    print(f"{'='*60}{RESET}\n")
    
    results = {}
    
    # Test 3a: ta_name exactly 127 bytes (should pass — < 128)
    log_info("Test 3a: ta_name = 127 bytes (should pass length check)")
    path_127 = b"/" + b"A" * 126
    resp = send_and_recv(CMD_LOAD_TA, path_127)
    results['path_127'] = resp
    time.sleep(0.3)
    
    # Test 3b: ta_name exactly 128 bytes (should FAIL — >= 128 → 0x3002)
    log_info("\nTest 3b: ta_name = 128 bytes (should fail with INVALID_PARAMETER)")
    path_128 = b"/" + b"A" * 127
    resp = send_and_recv(CMD_LOAD_TA, path_128)
    results['path_128'] = resp
    time.sleep(0.3)
    
    # Test 3c: ta_name = 200 bytes (should FAIL — >= 128 → 0x3002)
    log_info("\nTest 3c: ta_name = 200 bytes (definitely too long)")
    path_200 = b"/" + b"A" * 199
    resp = send_and_recv(CMD_LOAD_TA, path_200)
    results['path_200'] = resp
    time.sleep(0.3)
    
    # Test 3d: Maximum frame size (0x5000 bytes)
    log_info("\nTest 3d: Max frame size (0x5000 = 20480 bytes)")
    big_payload = b"\x00" * (MAX_FRAME - 4)
    resp = send_and_recv(0xDEADBEEF, big_payload)
    results['max_frame'] = resp
    time.sleep(0.3)
    
    return results

def test_4_error_code_differentiation():
    """Test 4: Error code analysis for file existence oracle"""
    print(f"\n{BOLD}{'='*60}")
    print(f"  TEST 4: Error Code Differentiation")
    print(f"{'='*60}{RESET}\n")
    
    test_paths = [
        # Known-existent system files
        (b"/system/build.prop\x00", "system/build.prop (exists, not a TA)"),
        (b"/vendor/build.prop\x00", "vendor/build.prop (exists, not a TA)"),
        (b"/proc/version\x00", "proc/version (exists, virtual)"),
        
        # Known-nonexistent paths
        (b"/nonexistent/file.mbn\x00", "nonexistent file"),
        (b"/tmp/zzz_no_such_file\x00", "temp nonexistent"),
        
        # Potential TA locations
        (b"/vendor/firmware/\x00", "vendor/firmware/ (directory)"),
        (b"/system/etc/firmware/\x00", "system/etc/firmware/ (directory)"),
        
        # Path traversal
        (b"../../../etc/passwd\x00", "path traversal to /etc/passwd"),
        (b"../../../../system/build.prop\x00", "traversal to build.prop"),
    ]
    
    results = {}
    for path, desc in test_paths:
        log_info(f"\nProbing: {desc}")
        resp = send_and_recv(CMD_LOAD_TA, path)
        results[desc] = resp
        
        if resp:
            # Analyze response
            if b"load ta succeed" in resp:
                log_ok(f"  → TA LOADED SUCCESSFULLY! Path: {path!r}")
            elif b"already loaded" in resp:
                log_warn(f"  → TA already loaded")
            else:
                log_info(f"  → Response ({len(resp)}B): {resp[:64]!r}")
        time.sleep(0.3)
    
    return results

def test_5_ta_discovery():
    """Test 5: Scan for TA binary files on the device"""
    print(f"\n{BOLD}{'='*60}")
    print(f"  TEST 5: TA Binary Discovery Scan")
    print(f"{'='*60}{RESET}\n")
    
    # Common Qualcomm QSEE TA paths
    ta_search_paths = [
        # Standard QSEE TA directories
        "/vendor/firmware_mnt/image/",
        "/vendor/firmware/",
        "/firmware/image/",
        "/system/etc/firmware/",
        "/persist/data/",
        
        # Common TA filenames (Qualcomm)
        "/vendor/firmware_mnt/image/cppf.mbn",
        "/vendor/firmware_mnt/image/tzhdcp.mbn",
        "/vendor/firmware_mnt/image/hdcp2p2.mbn",
        "/vendor/firmware_mnt/image/sampleauth.mbn",
        "/vendor/firmware_mnt/image/fidocrypto.mbn",
        "/vendor/firmware_mnt/image/fidocfg.mbn",
        "/vendor/firmware_mnt/image/qmiprivsys.mbn",
        "/vendor/firmware_mnt/image/chamomile.mbn",
        "/vendor/firmware_mnt/image/secotacl.mbn",
        "/vendor/firmware_mnt/image/soter.mbn",
        "/vendor/firmware_mnt/image/voiceprint.mbn",
        "/vendor/firmware_mnt/image/tz_ccm.mbn",
        "/vendor/firmware_mnt/image/tbase.mbn",
        "/vendor/firmware_mnt/image/playready.mbn",
        "/vendor/firmware_mnt/image/widevine.mbn",
        "/vendor/firmware_mnt/image/isdbtmm.mbn",
        "/vendor/firmware_mnt/image/gpqese.mbn",
        "/vendor/firmware_mnt/image/storsec.mbn",
        "/vendor/firmware_mnt/image/spdaemon.mbn",
        
        # Alternative locations
        "/vendor/lib/rfsa/adsp/",
        "/vendor/firmware_mnt/image/sec_img_auth.mbn",
        "/vendor/firmware_mnt/image/haventoken.mbn",
        "/vendor/firmware_mnt/image/kmsvc.mbn",
        
        # Qualcomm SM6375 specific
        "/vendor/firmware_mnt/image/qsee/",
        "/firmware/image/",
        
        # OEM-specific paths 
        "/vendor/etc/gate/",
        "/system/vendor/firmware/",
        "/data/vendor/firmware/",
    ]
    
    found = []
    not_found = []
    errors = []
    
    for path in ta_search_paths:
        path_bytes = path.encode() + b"\x00"
        if len(path_bytes) > 128:
            log_warn(f"Path too long (>{128}): {path}")
            continue
        
        log_info(f"Probing: {path}")
        resp = send_and_recv(CMD_LOAD_TA, path_bytes)
        
        if resp is None:
            errors.append(path)
        elif b"load ta succeed" in resp:
            log_ok(f"  ★ TA LOADED: {path}")
            found.append(path)
            # Unload immediately for next test
            time.sleep(0.2)
            send_and_recv(CMD_UNLOAD_TA)
        elif b"already loaded" in resp:
            log_ok(f"  ★ TA ALREADY LOADED: {path}")
            found.append(path)
        elif resp == b"" or len(resp) == 0:
            not_found.append(path)
        else:
            # Non-empty response but not success — file might exist but not a valid TA
            resp_text = resp.decode('ascii', errors='replace')
            if any(x in resp_text.lower() for x in ['fail', 'error', 'invalid']):
                log_warn(f"  → File may exist but load failed: {resp_text[:80]}")
                found.append(f"{path} (exists but invalid TA)")
            else:
                log_info(f"  → Unknown response: {resp!r}")
        
        time.sleep(0.2)
    
    print(f"\n{BOLD}{'─'*60}{RESET}")
    print(f"{BOLD}  SCAN RESULTS{RESET}")
    print(f"{'─'*60}")
    if found:
        print(f"\n{GREEN}  Found/Loadable TAs:{RESET}")
        for p in found:
            print(f"    ★ {p}")
    if not_found:
        print(f"\n{YELLOW}  Not Found:{RESET}")
        for p in not_found[:10]:
            print(f"    ✗ {p}")
        if len(not_found) > 10:
            print(f"    ... and {len(not_found)-10} more")
    if errors:
        print(f"\n{RED}  Errors:{RESET}")
        for p in errors:
            print(f"    ! {p}")
    
    return found

def test_6_exec_cmd_probing():
    """Test 6: If a TA was loaded, probe available commands"""
    print(f"\n{BOLD}{'='*60}")
    print(f"  TEST 6: TA Command Probing (requires loaded TA)")
    print(f"{'='*60}{RESET}\n")
    
    # First check if a TA is loaded by trying a simple exec
    log_info("Checking if a TA is loaded...")
    resp = send_and_recv(0x00000001, b"\x00" * 8)
    
    if resp and b"not already loaded" in resp:
        log_warn("No TA loaded. Load one first with test_5.")
        return {}
    
    log_ok("TA appears loaded. Probing commands...")
    
    # Try various cmd_ids
    test_cmds = [
        (0x00000001, b"\x00" * 16, "cmd=1 (generic query)"),
        (0x00000002, b"\x00" * 16, "cmd=2"),
        (0x00000003, b"\x00" * 16, "cmd=3"),
        (0x00000010, b"\x00" * 16, "cmd=0x10"),
        (0x00000100, b"\x00" * 16, "cmd=0x100"),
        (0x00001000, b"\x00" * 16, "cmd=0x1000"),
        (0x00010001, b"\x00" * 16, "cmd=0x10001"),
        (0x00010002, b"\x00" * 16, "cmd=0x10002"),
        (0x00010005, b"\x00" * 16, "cmd=0x10005"),
        (0x00010006, b"\x00" * 16, "cmd=0x10006"),
        (0xFFFFFFFF, b"\x00" * 16, "cmd=0xFFFFFFFF"),
    ]
    
    results = {}
    for cmd_id, payload, desc in test_cmds:
        log_info(f"\n{desc}:")
        resp = send_and_recv(cmd_id, payload)
        results[desc] = resp
        if resp:
            if b"succeed" in resp:
                log_ok(f"  → Command accepted!")
            elif len(resp) > 0:
                log_info(f"  → Response: {resp[:64].hex()}")
        time.sleep(0.3)
    
    return results

def test_7_persistent_session():
    """Test 7: Test persistent connection behavior"""
    print(f"\n{BOLD}{'='*60}")
    print(f"  TEST 7: Persistent Session Testing")
    print(f"{'='*60}{RESET}\n")
    
    sock = connect()
    if not sock:
        return False
    
    try:
        # Send multiple commands on same connection
        commands = [
            (CMD_UNLOAD_TA, b"", "UNLOAD"),
            (CMD_LOAD_TA, b"/nonexistent\x00", "LOAD nonexistent"),
            (CMD_UNLOAD_TA, b"", "UNLOAD again"),
        ]
        
        for cmd_id, payload, desc in commands:
            log_info(f"\n→ {desc}")
            send_cmd(sock, cmd_id, payload)
            resp = recv_response(sock)
            time.sleep(0.3)
        
        log_ok("Persistent connection test complete")
        return True
    except Exception as e:
        log_err(f"Error: {e}")
        return False
    finally:
        sock.close()

# ─── Main ───

def print_banner():
    print(f"""
{BOLD}{CYAN}╔══════════════════════════════════════════════════════════╗
║  GateServiced Protocol Validation & Testing Framework   ║
║  Phase 2: Dynamic Testing Suite                         ║
║  Target: TCP {HOST}:{PORT}                             ║
╚══════════════════════════════════════════════════════════╝{RESET}

  Protocol: [cmd_id:4B LE][payload:variable]
  Commands: 0x10003=LOAD_TA, 0x10004=UNLOAD_TA, other=EXEC
  Max frame: {MAX_FRAME} bytes
""")

def main():
    global HOST, VERBOSE
    
    # Parse args
    args = sys.argv[1:]
    test_name = None
    
    for arg in args:
        if arg.startswith("--host="):
            HOST = arg.split("=")[1]
        elif arg == "--quiet":
            VERBOSE = False
        elif arg == "--help":
            print("Usage: gate_protocol_test.py [--host=IP] [--quiet] [test_name]")
            print("Tests: connectivity, protocol, boundary, errorcode, discovery, exec, session, all")
            return
        else:
            test_name = arg
    
    print_banner()
    
    tests = {
        "connectivity": test_1_connectivity,
        "protocol": test_2_protocol_format,
        "boundary": test_3_boundary_checks,
        "errorcode": test_4_error_code_differentiation,
        "discovery": test_5_ta_discovery,
        "exec": test_6_exec_cmd_probing,
        "session": test_7_persistent_session,
    }
    
    if test_name and test_name != "all":
        if test_name in tests:
            tests[test_name]()
        else:
            log_err(f"Unknown test: {test_name}")
            log_info(f"Available: {', '.join(tests.keys())}, all")
    else:
        # Run all tests in order
        for name, func in tests.items():
            try:
                func()
            except Exception as e:
                log_err(f"Test '{name}' failed with exception: {e}")
            print()
    
    print(f"\n{BOLD}{'='*60}")
    print(f"  All tests complete.")
    print(f"{'='*60}{RESET}\n")

if __name__ == "__main__":
    main()
