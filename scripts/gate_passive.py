#!/usr/bin/env python3
"""
GateServiced Passive Listener
Wait for server to send data first
"""
import socket
import struct
import sys
import time

HOST = '127.0.0.1'
PORT = 18341  # Forwarded

def hexdump(data, prefix=""):
    if not data:
        return
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        print(f"{prefix}{i:04x}: {hex_str:<48} {ascii_str}")

def main():
    print("GateServiced Passive Listener")
    print(f"Connecting to {HOST}:{PORT}...")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    print("Connected! Waiting for server to send data...")
    
    sock.settimeout(10)
    try:
        # Wait for server to send first
        data = sock.recv(4096)
        if data:
            print(f"\nReceived {len(data)} bytes from server:")
            hexdump(data, "  ")
        else:
            print("Server closed connection without sending data")
    except socket.timeout:
        print("Timeout - server did not send data within 10 seconds")
    
    # Try sending after waiting
    print("\nNow trying to send commands...")
    
    # Command that previously worked (from logs): 
    # Looking for TA load command format
    
    # Try TZ command format from QSEE
    for cmd_id in [0x01, 0x02, 0x03, 0x10, 0x20, 0x30, 0x100, 0x200]:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((HOST, PORT))
            sock.settimeout(2)
            
            # Format: [total_len:4][cmd_id:4][sub_cmd:4][data_len:4]
            cmd = struct.pack('>IIII', 16, cmd_id, 0, 0)
            sock.sendall(cmd)
            print(f"Sent cmd 0x{cmd_id:02x}:", cmd.hex())
            
            try:
                resp = sock.recv(4096)
                if resp:
                    print(f"  Response ({len(resp)} bytes):")
                    hexdump(resp, "    ")
            except socket.timeout:
                print("  No response")
            
            sock.close()
            time.sleep(0.3)
        except Exception as e:
            print(f"  Error: {e}")
    
    print("\nDone")

if __name__ == "__main__":
    main()
