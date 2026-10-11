#!/usr/bin/env python3
"""
Qualcomm Sahara Protocol - Reset device from EDL 9008 mode (v2).
More aggressive approach: flushes buffers, toggles DTR/RTS, re-reads Hello,
tries sending reset in multiple ways.
"""
import serial
import struct
import sys
import time

SAHARA_HELLO        = 0x01
SAHARA_HELLO_RESP   = 0x02
SAHARA_READ_DATA    = 0x03
SAHARA_END_TRANSFER = 0x04
SAHARA_DONE         = 0x05
SAHARA_DONE_RESP    = 0x06
SAHARA_RESET        = 0x07
SAHARA_RESET_RESP   = 0x08
SAHARA_CMD_READY    = 0x0B

SAHARA_MODE_IMAGE_TX = 0x00
SAHARA_MODE_COMMAND  = 0x03

COM_PORT = "COM10"
BAUD_RATE = 115200

def sahara_pkt(cmd, data=b''):
    length = 8 + len(data)
    return struct.pack('<II', cmd, length) + data

def make_hello_resp(mode=SAHARA_MODE_COMMAND):
    """48-byte Hello Response packet"""
    pkt = struct.pack('<II', SAHARA_HELLO_RESP, 48)
    # version=2, compat=1, status=0, mode, reserved[6]
    pkt += struct.pack('<IIIIIIIII', 2, 1, 0, mode, 0, 0, 0, 0, 0)
    return pkt[:48]

def try_read(ser, size=256, timeout_s=2):
    """Try to read with timeout"""
    old_timeout = ser.timeout
    ser.timeout = timeout_s
    data = ser.read(size)
    ser.timeout = old_timeout
    return data

def parse_cmd(data):
    if len(data) < 8:
        return None
    cmd, length = struct.unpack('<II', data[:8])
    return cmd

def attempt_reset(ser):
    """Send Sahara RESET and check response"""
    print("[*] Sending SAHARA_RESET...")
    ser.write(sahara_pkt(SAHARA_RESET))
    time.sleep(0.5)
    resp = try_read(ser, 64, 3)
    if resp:
        cmd = parse_cmd(resp)
        if cmd == SAHARA_RESET_RESP:
            print("[+] RESET acknowledged! Device should reboot!")
            return True
        print(f"[*] Got response cmd=0x{cmd:02x}" if cmd else f"[*] Raw: {resp.hex()}")
    else:
        print("[*] No response (device may have already rebooted)")
    return False

def main():
    print(f"[*] EDL 9008 Reset Tool v2")
    print(f"[*] Opening {COM_PORT} @ {BAUD_RATE}...")
    
    try:
        ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=3,
                            dsrdtr=False, rtscts=False)
    except Exception as e:
        print(f"[!] Cannot open {COM_PORT}: {e}")
        return
    
    # Strategy 1: Flush and toggle DTR to retrigger Sahara Hello
    print("\n=== Strategy 1: DTR toggle + read Hello ===")
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    ser.dtr = False
    ser.rts = False
    time.sleep(0.5)
    ser.dtr = True
    ser.rts = True
    time.sleep(1)
    
    data = try_read(ser, 512, 3)
    if data:
        print(f"[*] Got {len(data)} bytes: {data[:64].hex()}")
        cmd = parse_cmd(data)
        if cmd == SAHARA_HELLO:
            print("[+] Sahara Hello received!")
            # Parse Hello
            if len(data) >= 48:
                ver, compat, maxcmd, mode = struct.unpack('<IIII', data[8:24])
                print(f"    ver={ver}, compat={compat}, maxcmd={maxcmd}, mode={mode}")
            
            # Send Hello Response -> command mode
            print("[*] Sending Hello Response (command mode)...")
            ser.write(make_hello_resp(SAHARA_MODE_COMMAND))
            time.sleep(0.5)
            
            resp = try_read(ser, 256, 3)
            if resp:
                rcmd = parse_cmd(resp)
                print(f"[*] Response cmd=0x{rcmd:02x}" if rcmd else f"[*] Raw: {resp.hex()}")
                if rcmd == SAHARA_READ_DATA:
                    print("[!] Device wants programmer - doesn't support command mode")
                    print("[*] Sending reset anyway...")
                elif rcmd == SAHARA_CMD_READY:
                    print("[+] Command mode active!")
            
            attempt_reset(ser)
            ser.close()
            return
        
        elif cmd == SAHARA_READ_DATA:
            print("[*] Device requesting programmer (already past Hello)")
            attempt_reset(ser)
            ser.close()
            return
        else:
            print(f"[*] Unknown cmd 0x{cmd:02x}")
    else:
        print("[!] No data from DTR toggle")
    
    # Strategy 2: Send Hello Response cold (device may be waiting)
    print("\n=== Strategy 2: Cold Hello Response ===")
    ser.reset_input_buffer()
    print("[*] Sending Hello Response directly...")
    ser.write(make_hello_resp(SAHARA_MODE_COMMAND))
    time.sleep(1)
    
    data = try_read(ser, 256, 3)
    if data:
        cmd = parse_cmd(data)
        print(f"[*] Got response cmd=0x{cmd:02x}" if cmd else f"[*] Raw: {data.hex()}")
    else:
        print("[!] No response to Hello Response")
    
    # Strategy 3: Just blast reset commands
    print("\n=== Strategy 3: Blast RESET commands ===")
    for i in range(5):
        ser.reset_input_buffer()
        attempt_reset(ser)
        time.sleep(0.5)
    
    # Strategy 4: Close port, wait, reopen
    print("\n=== Strategy 4: Port cycle ===")
    ser.close()
    time.sleep(2)
    
    try:
        ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=5)
        time.sleep(1)
        data = try_read(ser, 512, 5)
        if data:
            print(f"[*] After port cycle got {len(data)} bytes: {data[:64].hex()}")
            cmd = parse_cmd(data)
            if cmd == SAHARA_HELLO:
                print("[+] Got fresh Hello after port cycle!")
                ser.write(make_hello_resp(SAHARA_MODE_COMMAND))
                time.sleep(0.5)
                try_read(ser, 256, 2)
                attempt_reset(ser)
        else:
            print("[!] No data after port cycle")
            attempt_reset(ser)
        ser.close()
    except Exception as e:
        print(f"[!] Port cycle failed: {e}")
    
    print("\n[*] All strategies exhausted.")
    print("[*] If device is still in EDL, try:")
    print("    1. Unplug USB cable, hold POWER 15s, replug")
    print("    2. Or: unplug USB -> hold power -> replug USB -> run this script immediately")

if __name__ == '__main__':
    main()
