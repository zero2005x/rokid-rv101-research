#!/usr/bin/env python3
"""
Qualcomm Sahara Protocol - Reset device from EDL 9008 mode.
Sends a Sahara hello response then reset command to reboot the device.
"""
import serial
import struct
import sys
import time

# Sahara protocol commands
SAHARA_HELLO        = 0x01
SAHARA_HELLO_RESP   = 0x02
SAHARA_READ_DATA    = 0x03
SAHARA_END_TRANSFER = 0x04
SAHARA_DONE         = 0x05
SAHARA_DONE_RESP    = 0x06
SAHARA_RESET        = 0x07
SAHARA_RESET_RESP   = 0x08
SAHARA_CMD_READY    = 0x0B
SAHARA_CMD_SWITCH   = 0x0C
SAHARA_CMD_EXEC     = 0x0D
SAHARA_CMD_EXEC_RESP = 0x0E
SAHARA_CMD_EXEC_DATA = 0x0F

# Sahara modes
SAHARA_MODE_IMAGE_TX = 0x00
SAHARA_MODE_MEMORY_DEBUG = 0x01
SAHARA_MODE_COMMAND = 0x03

SAHARA_MAGIC = 0x7E4948534148  # Not used in newer versions

COM_PORT = "COM10"  # From device detection
BAUD_RATE = 115200

def sahara_packet(cmd, data=b''):
    """Create Sahara packet: [cmd:4][len:4][data]"""
    length = 8 + len(data)
    return struct.pack('<II', cmd, length) + data

def parse_sahara_header(data):
    """Parse Sahara packet header"""
    if len(data) < 8:
        return None, None, None
    cmd, length = struct.unpack('<II', data[:8])
    return cmd, length, data[8:length] if length > 8 else b''

def main():
    print(f"[*] Opening {COM_PORT}...")
    try:
        ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=5)
    except Exception as e:
        print(f"[!] Failed to open {COM_PORT}: {e}")
        return False
    
    print(f"[*] Port opened. Waiting for Sahara Hello...")
    
    # Read Sahara Hello from device
    time.sleep(0.5)
    data = ser.read(48)  # Sahara Hello is typically 48 bytes
    
    if len(data) < 8:
        print(f"[!] No data received from device (got {len(data)} bytes)")
        print(f"[*] Trying to send reset anyway...")
        # Send reset command directly
        reset_pkt = sahara_packet(SAHARA_RESET)
        ser.write(reset_pkt)
        time.sleep(1)
        resp = ser.read(64)
        print(f"[*] Response: {resp.hex() if resp else 'none'}")
        ser.close()
        return
    
    print(f"[*] Received {len(data)} bytes: {data[:48].hex()}")
    
    cmd, length, payload = parse_sahara_header(data)
    print(f"[*] Command: 0x{cmd:02x}, Length: {length}")
    
    if cmd == SAHARA_HELLO:
        # Parse Hello packet:
        # [cmd:4][len:4][version:4][version_compat:4][max_cmd_pkt_len:4]
        # [mode:4][reserved:24]
        if len(payload) >= 16:
            version, compat, max_len, mode = struct.unpack('<IIII', payload[:16])
            print(f"[*] Sahara Hello: version={version}, compat={compat}, max_len={max_len}, mode={mode}")
        
        # Send Hello Response - request command mode
        hello_resp = struct.pack('<II', SAHARA_HELLO_RESP, 48)
        # version, version_compat, status, mode, reserved[6]
        hello_resp += struct.pack('<IIIIIII', 2, 1, 0, SAHARA_MODE_COMMAND, 0, 0, 0)
        # Pad to 48 bytes
        hello_resp += b'\x00' * (48 - len(hello_resp))
        
        print(f"[*] Sending Hello Response (command mode)...")
        ser.write(hello_resp)
        time.sleep(0.5)
        
        # Read response
        resp = ser.read(64)
        if resp:
            resp_cmd, resp_len, resp_data = parse_sahara_header(resp)
            print(f"[*] Response: cmd=0x{resp_cmd:02x}, len={resp_len}")
            
            if resp_cmd == SAHARA_CMD_READY:
                print(f"[*] Device in command mode! Sending reset...")
            elif resp_cmd == SAHARA_READ_DATA:
                # Device wants a programmer file
                print(f"[*] Device requesting programmer file (mode switch not supported)")
                print(f"[*] Sending reset instead...")
            elif resp_cmd == SAHARA_END_TRANSFER:
                print(f"[*] Transfer ended. Sending reset...")
        
        # Send Reset command
        reset_pkt = sahara_packet(SAHARA_RESET)
        print(f"[*] Sending Sahara RESET command...")
        ser.write(reset_pkt)
        time.sleep(1)
        
        # Read reset response
        resp = ser.read(64)
        if resp:
            resp_cmd, _, _ = parse_sahara_header(resp)
            if resp_cmd == SAHARA_RESET_RESP:
                print(f"[+] Reset acknowledged! Device should reboot.")
            else:
                print(f"[*] Response: cmd=0x{resp_cmd:02x}")
        else:
            print(f"[*] No response (device may have already rebooted)")
    
    elif cmd == SAHARA_CMD_READY:
        print(f"[*] Device already in command mode!")
        reset_pkt = sahara_packet(SAHARA_RESET)
        ser.write(reset_pkt)
        time.sleep(1)
        print(f"[+] Reset sent.")
    
    else:
        print(f"[!] Unexpected command 0x{cmd:02x}")
        # Try reset anyway
        reset_pkt = sahara_packet(SAHARA_RESET)
        ser.write(reset_pkt)
        time.sleep(1)
    
    ser.close()
    print(f"[*] Done. Check if device reboots...")
    return True

if __name__ == '__main__':
    main()
