#!/usr/bin/env python3
"""
Frida hook for GateServiced
"""
import frida
import sys
import time

HOOK_SCRIPT = """
console.log("[*] Hooking GateServiced (PID: " + Process.id + ")");

var libcName = "libc.so";

// Hook accept4
try {
    Interceptor.attach(Module.findExportByName(libcName, "accept4"), {
        onEnter: function(args) {
            this.fd = args[0].toInt32();
        },
        onLeave: function(retval) {
            var clientFd = retval.toInt32();
            if (clientFd >= 0) {
                console.log("[accept4] listen_fd=" + this.fd + " client_fd=" + clientFd);
            }
        }
    });
    console.log("[+] Hooked accept4");
} catch(e) { console.log("[-] accept4 hook failed: " + e); }

// Hook read
try {
    Interceptor.attach(Module.findExportByName(libcName, "read"), {
        onEnter: function(args) {
            this.fd = args[0].toInt32();
            this.buf = args[1];
            this.count = args[2].toInt32();
        },
        onLeave: function(retval) {
            var ret = retval.toInt32();
            if (ret > 0 && this.fd > 2) {
                console.log("[read] fd=" + this.fd + " ret=" + ret);
                try {
                    var data = Memory.readByteArray(this.buf, Math.min(ret, 128));
                    console.log(hexdump(data, {length: Math.min(ret, 128), header: false, ansi: false}));
                } catch(e) {}
            }
        }
    });
    console.log("[+] Hooked read");
} catch(e) { console.log("[-] read hook failed: " + e); }

// Hook recv
try {
    Interceptor.attach(Module.findExportByName(libcName, "recv"), {
        onEnter: function(args) {
            this.fd = args[0].toInt32();
            this.buf = args[1];
        },
        onLeave: function(retval) {
            var ret = retval.toInt32();
            if (ret > 0) {
                console.log("[recv] fd=" + this.fd + " ret=" + ret);
                try {
                    var data = Memory.readByteArray(this.buf, Math.min(ret, 128));
                    console.log(hexdump(data, {length: Math.min(ret, 128), header: false, ansi: false}));
                } catch(e) {}
            }
        }
    });
    console.log("[+] Hooked recv");
} catch(e) { console.log("[-] recv hook failed: " + e); }

// Hook send
try {
    Interceptor.attach(Module.findExportByName(libcName, "send"), {
        onEnter: function(args) {
            var fd = args[0].toInt32();
            var buf = args[1];
            var len = args[2].toInt32();
            if (len > 0) {
                console.log("[send] fd=" + fd + " len=" + len);
                try {
                    var data = Memory.readByteArray(buf, Math.min(len, 128));
                    console.log(hexdump(data, {length: Math.min(len, 128), header: false, ansi: false}));
                } catch(e) {}
            }
        }
    });
    console.log("[+] Hooked send");
} catch(e) { console.log("[-] send hook failed: " + e); }

// Hook write
try {
    Interceptor.attach(Module.findExportByName(libcName, "write"), {
        onEnter: function(args) {
            var fd = args[0].toInt32();
            var buf = args[1];
            var count = args[2].toInt32();
            if (fd > 2 && count > 0) {
                console.log("[write] fd=" + fd + " count=" + count);
                try {
                    var data = Memory.readByteArray(buf, Math.min(count, 128));
                    console.log(hexdump(data, {length: Math.min(count, 128), header: false, ansi: false}));
                } catch(e) {}
            }
        }
    });
    console.log("[+] Hooked write");
} catch(e) { console.log("[-] write hook failed: " + e); }

console.log("[*] Hooks installed, waiting for activity...");
"""

def on_message(message, data):
    if message['type'] == 'send':
        print(f"[Script] {message['payload']}")
    elif message['type'] == 'error':
        print(f"[Error] {message['stack']}")
    else:
        print(f"[Message] {message}")

def main():
    try:
        # Connect to USB device
        device = frida.get_usb_device(timeout=5)
        print(f"[*] Connected to: {device.name}")
        
        # Find GateServiced
        for proc in device.enumerate_processes():
            if "GateServiced" in proc.name:
                print(f"[*] Found GateServiced: PID {proc.pid}")
                
                # Attach
                print("[*] Attaching...")
                session = device.attach(proc.pid)
                
                print("[*] Loading script...")
                script = session.create_script(HOOK_SCRIPT)
                script.on('message', on_message)
                script.load()
                
                print("[*] Script loaded. Press Ctrl+C to stop.")
                print("[*] Trigger connections to port 8341 to see activity.\n")
                
                try:
                    while True:
                        time.sleep(1)
                except KeyboardInterrupt:
                    print("\n[*] Detaching...")
                    session.detach()
                
                return
        
        print("[-] GateServiced not found")
        print("Available processes:")
        for proc in device.enumerate_processes()[:20]:
            print(f"  {proc.pid}: {proc.name}")
            
    except frida.NotSupportedError as e:
        print(f"[-] Not supported: {e}")
    except frida.ProcessNotFoundError as e:
        print(f"[-] Process not found: {e}")
    except Exception as e:
        print(f"[-] Error: {e}")

if __name__ == "__main__":
    main()
