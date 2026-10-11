import socket, struct, sys, time, binascii

HOST = "127.0.0.1"
PORT = 18341  # adb forward

def send_cmd(cmd_id, payload=b"", timeout=3.0):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect((HOST, PORT))
    s.sendall(struct.pack("<I", cmd_id) + payload)
    data = b""
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
            if len(chunk) < 65536:
                break
    except socket.timeout:
        pass
    s.close()
    return data

def hexdump(b, maxlen=128):
    h = binascii.hexlify(b[:maxlen]).decode()
    return " ".join(h[i:i+2] for i in range(0, len(h), 2))

def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "verify"
    if mode == "verify":
        for cmd, name in [(0x10004, "UNLOAD_TA"), (0x10003, "LOAD_TA(empty)")]:
            r = send_cmd(cmd, b"" if cmd == 0x10004 else b"/etc/passwd\x00")
            print(f"{name}: rcvd {len(r)} bytes: {r[:64]!r} | {hexdump(r)}")
    elif mode == "enum":
        # scan cmd_ids, no payload
        for cmd in range(0, 0x110):
            try:
                r = send_cmd(cmd, timeout=0.8)
                if r:
                    print(f"cmd=0x{cmd:04x}: {r[:32]!r}")
            except Exception as e:
                print(f"cmd=0x{cmd:04x}: EXC {e}")
    elif mode == "load":
        path = sys.argv[2].encode() if len(sys.argv) > 2 else b"/etc/passwd"
        r = send_cmd(0x10003, path)
        print(f"LOAD_TA {path!r}: {r[:64]!r}")
    elif mode == "exec":
        cmd_id = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x1
        payload = sys.argv[3].encode() if len(sys.argv) > 3 else b""
        r = send_cmd(cmd_id, payload)
        print(f"EXEC_CMD 0x{cmd_id:x}: {r[:128]!r} | {hexdump(r, 256)}")
    elif mode == "scan":
        # load each path, logcat distinguishes result
        for path in sys.argv[2:]:
            r = send_cmd(0x10003, path.encode())
            print(f"LOAD_TA {path}: rcvd {len(r)}: {r[:32]!r}")
        cmd_id = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x1
        payload = sys.argv[3].encode() if len(sys.argv) > 3 else b""
        r = send_cmd(cmd_id, payload)
        print(f"EXEC_CMD 0x{cmd_id:x}: {r[:128]!r} | {hexdump(r, 256)}")

if __name__ == "__main__":
    main()
