#!/usr/bin/env python3
"""
GateServiced Protocol Analyzer
Connect to GateServiced and analyze protocol
"""
import socket
import struct
import sys
import time

HOST = '127.0.0.1'
PORT = 18341  # Forwarded from device:8341

def hexdump(data, prefix=""):
    if not data:
        print(f"{prefix}(empty)")
        return
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        print(f"{prefix}{i:04x}: {hex_str:<48} {ascii_str}")

def send_command(sock, cmd_data, timeout=3):
    """Send command and receive response"""
    try:
        sock.settimeout(timeout)
        sock.sendall(cmd_data)
        print(f"TX ({len(cmd_data)} bytes):")
        hexdump(cmd_data, "  ")
        
        # Wait for response
        response = b''
        try:
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response += chunk
        except socket.timeout:
            pass
        
        if response:
            print(f"RX ({len(response)} bytes):")
            hexdump(response, "  ")
        else:
            print("RX: (no response)")
        return response
    except Exception as e:
        print(f"Error: {e}")
        return None

def try_protocol(sock, name, data):
    """Try a protocol variant"""
    print(f"\n=== {name} ===")
    return send_command(sock, data)

def main():
    print("GateServiced Protocol Analyzer")
    print(f"Target: {HOST}:{PORT}")
    print("=" * 50)
    
    # Connect
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))
        print(f"Connected to {HOST}:{PORT}")
    except Exception as e:
        print(f"Connection failed: {e}")
        return 1
    
    # Test various protocol formats
    tests = [
        # Format: (name, data)
        ("Null (4 bytes)", b'\x00\x00\x00\x00'),
        ("Length=1, cmd=0", struct.pack('>I', 1) + b'\x00'),
        ("Length=4, cmd=0x01", struct.pack('>II', 4, 1)),
        ("Length=4, cmd=0x10", struct.pack('>II', 4, 0x10)),
        ("Length=8, cmd=0x01, arg=0", struct.pack('>III', 8, 1, 0)),
        ("LE Length=4, cmd=1", struct.pack('<II', 4, 1)),
        ("Type+Length", struct.pack('>HHI', 1, 4, 0)),
        ("GATE magic", b'GATE\x00\x00\x00\x00'),
        ("Version request", struct.pack('>IBBB', 0, 0, 0, 1)),
    ]
    
    for name, data in tests:
        try:
            # Create new connection for each test
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((HOST, PORT))
            try_protocol(sock, name, data)
            sock.close()
            time.sleep(0.5)
        except Exception as e:
            print(f"Test '{name}' failed: {e}")
    
    print("\n" + "=" * 50)
    print("Analysis complete")
    return 0

if __name__ == "__main__":
    sys.exit(main())
