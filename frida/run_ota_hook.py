#!/usr/bin/env python3
"""run_ota_hook.py — attach com.rokid.glass.ota + 載入 ota_hook.js 並常駐收 log。"""
import frida
import sys
import time

HOST = "172.26.96.1:28042"
PACKAGE = "com.rokid.glass.ota"
SCRIPT = "/home/kali/rokid_root/frida/ota_hook.js"


def on_message(message, data):
    if message.get("type") == "log":
        print(message["payload"], flush=True)
    elif message.get("type") == "error":
        print("[ERR]", message.get("description"), flush=True)
    else:
        print("[MSG]", message, flush=True)


def main():
    mgr = frida.get_device_manager()
    device = mgr.add_remote_device(HOST)
    session = device.attach(PACKAGE)
    with open(SCRIPT) as f:
        code = f.read()
    script = session.create_script(code)
    script.on("message", on_message)
    script.load()
    print(f"[*] attached to {PACKAGE}, hooks loaded. Ctrl+C to stop.", flush=True)
    try:
        while True:
            time.sleep(1)  # 常駐收 log
    except KeyboardInterrupt:
        pass
    finally:
        script.unload()
        session.detach()


if __name__ == "__main__":
    main()
