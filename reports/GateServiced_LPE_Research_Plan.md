# GateServiced Local Privilege Escalation (LPE) Research Plan

**Target**: Rokid AR Glasses (RV101) — Android 12, Kernel 5.10.209  
**Primary Vector**: `GateServiced` @ `/vendor/bin/GateServiced` (20,304 bytes, AArch64)  
**Date**: February 10, 2026

---

## 1. Executive Summary — Why GateServiced Matters

From firmware analysis of `RVE01-ota-1.14.006-20260204-150203.zip`, I confirmed:

### 1.1 GateServiced Configuration (from `init.gateserviced.rc`)

```
service GateServiced /vendor/bin/GateServiced
    user root
    group root system
    disabled

on property:sys.boot_completed=1
    start GateServiced
```

**Critical findings:**

- **Runs as `root:root`** — any code execution = full root
- **No `seclabel` override** — inherits `tee_exec` → transitions to `tee` SELinux domain
- **No capabilities restriction** — no `capabilities` directive = ALL root capabilities
- **Started on every boot** — `on property:sys.boot_completed=1`
- **Listens on TCP `0.0.0.0:8341`** — confirmed by strings + previous recon (`netstat` on device)

### 1.2 SELinux Domain Analysis (from `vendor_sepolicy.cil`)

The `tee` domain (GateServiced's runtime domain) has:

```cil
(allow tee_32_0 self (capability (dac_override)))
(allow tee_32_0 self (capability (chown setgid setuid sys_rawio sys_admin)))
(allow tee_32_0 self (capability2 (block_suspend)))
(allow tee_32_0 self (tcp_socket (read write create getattr setattr lock append
    map bind connect listen accept getopt setopt shutdown)))
(allow tee_32_0 node_32_0 (tcp_socket (node_bind)))
(allow tee_32_0 port_32_0 (tcp_socket (name_bind)))
```

**Translation:** GateServiced runs as root with:

- `CAP_DAC_OVERRIDE` — bypass all file permission checks
- `CAP_CHOWN, CAP_SETGID, CAP_SETUID` — change any file owner, switch UID/GID
- `CAP_SYS_RAWIO` — raw I/O port access
- `CAP_SYS_ADMIN` — the "god" capability (mount, namespace, etc.)
- **Full TCP socket permissions** including `bind`, `listen`, `accept` on any port
- Access to `/dev/block/` devices, RPMB, SSD block devices, DMA buffers
- Read/write to `vendor_persist_*`, `tee_data_file`, `vendor_data_tzstorage_file`
- **Is a `halclientdomain`** — can call HAL services including graphics, display, etc.

### 1.3 Attack Surface Classification

| Surface            | Access from Shell                  | Risk Level   |
| ------------------ | ---------------------------------- | ------------ |
| TCP 0.0.0.0:8341   | ✅ `nc localhost 8341`             | **CRITICAL** |
| QSEE/TEE interface | ❌ `tee_device` only               | Medium       |
| DMA-Buf allocator  | ❌ Requires tee context            | Low          |
| vndbinder IPC      | ⚠️ Depends on service registration | Medium       |

**Shell (uid 2000) can reach GateServiced via TCP localhost:8341** — this is our primary attack vector.

---

## 2. Binary Analysis — What We Know from Strings

### 2.1 Library Dependencies

| Library                | Purpose                                   |
| ---------------------- | ----------------------------------------- |
| `libc.so`              | Standard C library                        |
| `liblog.so`            | Android logging                           |
| `libcutils.so`         | Android C utilities                       |
| `libutils.so`          | Android utility library                   |
| `libdmabufheap.so`     | DMA-Buf heap allocation                   |
| `libminkdescriptor.so` | **Qualcomm TrustZone/QSEE IPC** (MinkIPC) |
| `libdl.so`             | Dynamic loader                            |
| `libm.so`              | Math library                              |

### 2.2 Reconstructed Architecture from Strings

```
                 ┌──────────────────────┐
                 │  ADB Shell (uid=2000)│
                 │  nc localhost 8341   │
                 └────────┬─────────────┘
                          │ TCP
                 ┌────────▼─────────────┐
                 │   GateServiced       │
                 │   (uid=0, root)      │
                 │                      │
                 │  main()              │
                 │  ├─ socket()         │
                 │  ├─ bind(8341)       │
                 │  ├─ listen()         │
                 │  ├─ accept()         │
                 │  └─ handle_client()  │
                 │      ├─ recvfrom()   │
                 │      ├─ cmd_dispatch()│──── Key function!
                 │      │  ├─ run_load_ta()
                 │      │  ├─ run_exec_cmd_ta()
                 │      │  ├─ run_unload_ta()
                 │      │  ├─ run_shared_memory_example()
                 │      │  ├─ get_ta_version()
                 │      │  └─ read_file()  ◄── File read primitive!
                 │      └─ sendto()     │
                 └────────┬─────────────┘
                          │ MinkIPC / QSEE
                 ┌────────▼─────────────┐
                 │   TrustZone QSEE     │
                 │   Trusted App (TA)   │
                 └──────────────────────┘
```

### 2.3 Key Function Signatures (from log strings)

| Function          | Log Pattern                                        | Analysis                                                   |
| ----------------- | -------------------------------------------------- | ---------------------------------------------------------- |
| `cmd_dispatch`    | `"cmd_id = %x, input_len = %zu, output_len = %zu"` | **Command router — takes cmd_id and variable I/O buffers** |
| `run_load_ta`     | `"Load app %s failed: %d"`                         | **Loads TA by path — potential path traversal!**           |
| `run_exec_cmd_ta` | `"cmd_id = %x, cmd_dispatch, times = %lld"`        | Forwards cmd to loaded TA                                  |
| `run_unload_ta`   | `"unload ta called"`                               | Unloads TA                                                 |
| `read_file`       | `"Failed to open file %s: %s (%d)"`                | **Reads files — as root!**                                 |
| `get_file_size`   | `"Failed telling size of file %s"`                 | Gets file size                                             |
| `get_ta_version`  | `"ta version = %s"`                                | Returns TA version string                                  |
| `load_app`        | `"Getting the application object failed"`          | QSEE app loader                                            |
| `handle_client`   | `"wait for client data, client_fd:%d"`             | Per-connection handler (threaded)                          |

### 2.4 Protocol Format (confirmed from prior fuzzer scripts)

```
Wire format: [cmd_id:4B LE][total_len:4B LE][payload:variable]

Response: Similar header + response data

Known cmd_ids:
- cmd for load_ta   → requires TA name/path in payload
- cmd for exec_cmd  → requires loaded TA + sub-command
- cmd for unload_ta → unload current TA
- cmd for get_ta_version → returns version string
```

### 2.5 Error Codes (QSEE-style, confirmed by fuzzer)

| Code   | Meaning                                  |
| ------ | ---------------------------------------- |
| 0x0000 | SUCCESS                                  |
| 0x3001 | GENERAL_ERROR                            |
| 0x3002 | INVALID_PARAMETER                        |
| 0x3003 | NOT_SUPPORTED                            |
| 0x300c | **TA_NOT_LOADED** (most common response) |
| 0x300d | TA_DEAD                                  |
| 0x300f | CMD_NOT_SUPPORTED                        |

---

## 3. Vulnerability Hypotheses — Prioritized Attack Vectors

### 🔴 Priority 1: `read_file` Arbitrary File Read (as root)

**Evidence:**

```
[GATE CA] "Failed to open file %s: %s (%d)"
[GATE CA] "Failed reading the file %s: %zu vs %zu bytes: %s (%d)"
[GATE CA] "Failed seeking in file %s: %s (%d)"
[GATE CA] "Failed telling size of file %s: %s (%d)"
```

The `read_file()` function opens and reads files. Since GateServiced runs as root with `CAP_DAC_OVERRIDE`:

- If the file path comes from the TCP client → **arbitrary file read as root**
- Could read `/data/misc/adb/adb_keys` to inject ADB authorization
- Could read `/data/system/packages.xml` for app secrets
- Could read `/proc/kallsyms` (bypasses kptr_restrict from root)
- **Most critically**: Could read `/dev/block/by-name/boot_a` (boot partition)

**Research approach:**

1. Identify which `cmd_id` triggers `read_file`
2. Determine if the file path is client-controlled
3. Determine if the file contents are returned to the client

### 🔴 Priority 2: `run_load_ta` Path Traversal → Arbitrary Code Execution

**Evidence:**

```
[GATE CA] "Load app %s failed: %d"
[GATE CA] "TA appPath is NULL!"
[GATE CA] "data_len exceeds maximum length for ta_name"
```

The `run_load_ta()` function loads a Trusted App by path. If the path is client-supplied:

1. We could path-traverse out of the TA directory
2. Push our own "TA" binary (actually a crafted ELF/TA file) to a writable location
3. Tell GateServiced to load it

Even if QSEE validates TA signatures, a crafted but invalid TA could cause:

- Buffer overflow in path handling (`ta_name` length check exists but may be bypassable)
- Denial of service → service crash → potential for race condition exploitation

### 🟡 Priority 3: `cmd_dispatch` Command Injection

**Evidence:**

```
[GATE CA] "Received, cmd_id: %x, rsv_len:%zu"
[GATE CA] "cmd_dispatch failed, ret:%x"
```

The `cmd_dispatch()` function routes commands by `cmd_id`. Key questions:

- Is there a command ID that directly passes data to `system()` or `exec()`?
- Are there undocumented/debug commands?
- What happens with malformed `cmd_id` values (integer overflow, negative)?

### 🟡 Priority 4: Magic Number Validation Bypass

**Evidence:**

```
[GATE CA] "received magicNumTA failed,magicNumTA= %x"
```

There's a magic number check for TAs. If we can determine the expected magic number, we can craft payloads that pass this check.

### 🟢 Priority 5: DMA-Buf/Shared Memory Exploitation

**Evidence:**

```
"CreateDmabufHeapBufferAllocator() failed"
"Error: DMA-Buf allocation failed from heap %d, len %zu"
[GATE CA] "Mapping sharedMemObj failed!"
[GATE CA] "dma_alloc failed with return value =%d"
```

The `run_shared_memory_example()` function creates DMA-Buf allocations. If we can influence the allocation parameters via the TCP protocol, integer overflow in `len` could cause:

- Heap overflow via undersized allocation
- Information leak via over-read

---

## 4. Ghidra/IDA Analysis Guide

### 4.1 Setup

```
Binary: vendor_extracted/bin/GateServiced (20,304 bytes)
Architecture: AArch64 (ARM64)
Processor: ARM:AARCH64:v8A (Ghidra) or ARM64 (IDA)
Base address: Let tool auto-detect (PIE binary)
```

### 4.2 Critical Symbols to Find

| Symbol            | What to Look For                                        |
| ----------------- | ------------------------------------------------------- |
| `main`            | Entry point → socket setup → accept loop                |
| `cmd_dispatch`    | Command routing table (switch/jump table)               |
| `handle_client`   | Per-connection handler, input parsing                   |
| `read_file`       | File path parameter source (stack? heap? client input?) |
| `run_load_ta`     | TA path construction, validation                        |
| `run_exec_cmd_ta` | Command forwarding to QSEE                              |
| `get_ta_version`  | Simple command, good for protocol validation            |
| `load_app`        | `TZCom_getClientEnvObject` → QSEE interaction           |

### 4.3 String Cross-References to Trace

```
"cmd_id = %x, input_len = %zu, output_len = %zu"
  → Find xref → cmd_dispatch function → trace switch cases

"Load app %s failed: %d"
  → Find xref → run_load_ta → trace path parameter source

"Failed to open file %s"
  → Find xref → read_file → trace filename parameter

"data_len exceeds maximum length for ta_name"
  → Find xref → length validation code → check for off-by-one

"received magicNumTA failed,magicNumTA= %x"
  → Find xref → magic number comparison → extract expected value
```

### 4.4 Key Patterns in Disassembly

```c
// Look for switch/jump table in cmd_dispatch:
//   cmp    w0, #0x5        // cmd_id range check
//   b.hi   default_case
//   adr    x1, jump_table
//   ldrh   w2, [x1, w0, uxtw #1]
//   add    x1, x1, w2, sxth #2
//   br     x1

// Look for unchecked memcpy in handle_client:
//   ldr    w0, [sp, #0x10]  // client_supplied_length
//   bl     memcpy           // no bounds check?

// Look for format string in response:
//   adr    x0, fmt_string
//   bl     __android_log_print
//   // Is there a snprintf to response buffer with user data?
```

---

## 5. Live Testing Protocol — On-Device Commands

### 5.1 Fingerprinting (from ADB shell)

```bash
# Verify GateServiced is running
ps -A | grep -i gate
# Expected: root ... /vendor/bin/GateServiced

# Verify listening port
netstat -tlnp 2>/dev/null | grep 8341
# Expected: tcp 0 0 0.0.0.0:8341 0.0.0.0:* LISTEN pid/GateServiced

# Test connectivity
echo "AAAA" | nc -w 2 localhost 8341 | xxd
# Should connect (SELinux allows shell TCP)
```

### 5.2 Protocol Probe Script (push to device)

```python
#!/system/bin/sh
# gateservice_probe.sh — Run on glasses via adb shell

# Test 1: Empty connection
(echo -n "" | nc -w 1 localhost 8341) 2>&1 | xxd | head

# Test 2: Minimal cmd_id probes (little-endian)
for CMD in 00 01 02 03 04 05 10 20 ff; do
    echo -ne "\x${CMD}\x00\x00\x00\x08\x00\x00\x00" | \
        nc -w 1 localhost 8341 | xxd
done

# Test 3: Known cmd_id with "load_ta" attempt
# cmd_id=0x01 (hypothetical load), path="/vendor/firmware/test.mbn"
echo -ne "\x01\x00\x00\x00\x24\x00\x00\x00/vendor/firmware/test.mbn\x00" | \
    nc -w 1 localhost 8341 | xxd
```

### 5.3 Advanced Python Probe (via ADB port forward)

```bash
# Terminal 1 (Windows/RPi4):
adb forward tcp:18341 tcp:8341

# Terminal 2 (local):
python gate_fuzzer_v3.py  # Already in workspace
```

---

## 6. Attack Chaining Strategies

### Chain A: GateServiced → Arbitrary File Write → Root

```
1. Fuzz cmd_ids to find "write_file" or "save_ta" command
2. If exists: Write our own /data/adb/modules/su binary
3. Or: Overwrite /system/xbin/su with a setuid root shell
4. Or: Write to /data/misc/adb/adb_keys (add our pubkey)
```

### Chain B: GateServiced → Arbitrary File Read → Boot Extraction → Magisk

```
1. Find cmd_id for read_file
2. Read /dev/block/by-name/boot_a (96 MB)
3. BUT: We already have boot.img from OTA — this isn't needed
4. More useful: Read /data/misc/adb/adb_keys, /init.rc, etc.
```

### Chain C: GateServiced → Load Malicious TA → Code Execution

```
1. Push crafted "TA" to /data/local/tmp/evil.mbn
2. Send load_ta command with path "/data/local/tmp/evil.mbn"
3. If TA loading crashes/exploits GateServiced → code exec as root
4. Even simple crash → respawn race → file replacement attack
```

### Chain D: GateServiced → Buffer Overflow → RCE

```
1. Send oversized ta_name bypassing length check
2. Overflow stack buffer → ROP chain
3. ASLR is enabled, but binary is small (20KB) → limited gadgets
4. Need info leak first (from read_file or response data)
```

### Chain E: Confused Deputy via System Apps

```
1. CXRService (priv-app) — exported components?
2. RokidSysConfig (priv-app) — can set system properties?
3. If either can set vendor.* properties or interact with GateServiced
   → Use as confused deputy to gain privileged access
```

---

## 7. Greppable Pattern Checklist

Run these against the extracted firmware:

```bash
# On extracted vendor partition:

# 7.1 Find all TCP-listening services
grep -rn "bind\|listen\|accept\|SOCK_STREAM" vendor_extracted/bin/ --include="*.so" 2>/dev/null
strings vendor_extracted/bin/* | grep -i "port\|bind\|listen\|0\.0\.0\.0"

# 7.2 Find all root services
grep -rn "user root" vendor_extracted/etc/init/*.rc

# 7.3 Find services without SELinux seclabel
grep -L "seclabel" vendor_extracted/etc/init/*.rc | while read f; do
    grep -l "service " "$f"
done

# 7.4 Find system() or exec() calls in binaries
strings vendor_extracted/bin/GateServiced | grep -i "system\|exec\|popen\|fork"

# 7.5 Find hardcoded paths in GateServiced
strings vendor_extracted/bin/GateServiced | grep "/"

# 7.6 Find property-triggered service starts
grep -rn "on property:" vendor_extracted/etc/init/*.rc | grep -i "start\|exec"

# 7.7 Find world-writable or shell-accessible file paths
grep -rn "mode 0666\|mode 0777\|chmod 666\|chmod 777" vendor_extracted/etc/init/*.rc

# 7.8 Find socket declarations
grep -rn "socket " vendor_extracted/etc/init/*.rc

# 7.9 Find IPC services (binder/vndbinder)
strings vendor_extracted/bin/* | grep -i "defaultServiceManager\|addService\|getService"

# 7.10 SELinux policy gaps for shell→tee interaction
grep "shell.*tee\|tee.*shell" vendor_extracted/etc/selinux/vendor_sepolicy.cil
```

---

## 8. Other Root Services Worth Investigating

| Service             | User        | Binary Size  | Notes                                                 |
| ------------------- | ----------- | ------------ | ----------------------------------------------------- |
| `GateServiced`      | root:root   | **20,304 B** | TCP 8341, QSEE proxy — **PRIMARY TARGET**             |
| `qttestservice`     | root:media  | 15,112 B     | **Disabled, starts via property** — potential trigger |
| `poweropt-service`  | root:system | 20,328 B     | Qualcomm power optimizer                              |
| `qguard`            | root:system | 127,128 B    | Qualcomm process guardian, has `CAP_KILL`             |
| `thermal-engine-v2` | root        | 3,477,552 B  | Huge binary, complex                                  |
| `qseecomd`          | root        | 32,904 B     | QSEE communication daemon                             |
| `hal_proxy_daemon`  | —           | 208,296 B    | HAL proxy, check run context                          |

### `qttestservice` — Secondary Vector

```rc
on property:vendor.qttestservice=enable
    start qttestservice
```

If shell can set `vendor.qttestservice=enable` (check SELinux), this starts a root service on demand. The service itself (`15,112 bytes`) is tiny and likely simple — worth reversing for debug/test commands.

---

## 9. Conceptual PoC Structure

### PoC 1: GateServiced Protocol Explorer

```c
/**
 * gateserviced_explorer.c — Systematic protocol exploration
 * Cross-compile: aarch64-linux-gnu-gcc -static -o gateserviced_explorer gateserviced_explorer.c
 * Push: adb push gateserviced_explorer /data/local/tmp/
 * Run: adb shell /data/local/tmp/gateserviced_explorer
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8341
#define MAX_RESP 65536

typedef struct {
    uint32_t cmd_id;
    uint32_t total_len;   // header(8) + payload_len
    // variable payload follows
} __attribute__((packed)) gate_header_t;

int send_cmd(int cmd_id, const void *payload, size_t payload_len,
             void *resp, size_t *resp_len) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = inet_addr("127.0.0.1")
    };

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }

    gate_header_t hdr = {
        .cmd_id = cmd_id,
        .total_len = 8 + payload_len
    };

    send(sock, &hdr, sizeof(hdr), 0);
    if (payload_len > 0)
        send(sock, payload, payload_len, 0);

    struct timeval tv = { .tv_sec = 2 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    *resp_len = recv(sock, resp, MAX_RESP, 0);
    close(sock);
    return (*resp_len > 0) ? 0 : -1;
}

int main() {
    uint8_t resp[MAX_RESP];
    size_t resp_len;

    printf("[*] GateServiced Protocol Explorer\n");
    printf("[*] Target: 127.0.0.1:%d\n\n", PORT);

    // Phase 1: Enumerate valid cmd_ids
    printf("=== Phase 1: cmd_id enumeration ===\n");
    for (int cmd = 0; cmd <= 0xFF; cmd++) {
        if (send_cmd(cmd, NULL, 0, resp, &resp_len) == 0) {
            printf("[+] cmd=0x%02x resp_len=%zu: ", cmd, resp_len);
            for (size_t i = 0; i < (resp_len < 32 ? resp_len : 32); i++)
                printf("%02x ", resp[i]);
            printf("\n");
        }
    }

    // Phase 2: Test load_ta with known TA paths
    printf("\n=== Phase 2: TA path probing ===\n");
    const char *ta_paths[] = {
        "/vendor/firmware/",
        "/vendor/lib64/",
        "/data/local/tmp/test.mbn",
        "../../../data/local/tmp/test",
        NULL
    };

    for (int i = 0; ta_paths[i]; i++) {
        // Try common load cmd_ids (0x01, 0x02, 0x10, 0x100)
        int load_cmds[] = {0x01, 0x02, 0x10, 0x100};
        for (int j = 0; j < 4; j++) {
            if (send_cmd(load_cmds[j], ta_paths[i], strlen(ta_paths[i])+1,
                        resp, &resp_len) == 0) {
                printf("[+] cmd=0x%x path='%s' resp_len=%zu: ",
                       load_cmds[j], ta_paths[i], resp_len);
                for (size_t k = 0; k < (resp_len < 16 ? resp_len : 16); k++)
                    printf("%02x ", resp[k]);
                printf("\n");
            }
        }
    }

    // Phase 3: Test file read
    printf("\n=== Phase 3: file read probing ===\n");
    const char *read_targets[] = {
        "/etc/passwd",
        "/system/build.prop",
        "/proc/version",
        NULL
    };

    for (int i = 0; read_targets[i]; i++) {
        // Try hypothetical read cmd_ids
        int read_cmds[] = {0x03, 0x04, 0x05, 0x20, 0x30};
        for (int j = 0; j < 5; j++) {
            if (send_cmd(read_cmds[j], read_targets[i],
                        strlen(read_targets[i])+1, resp, &resp_len) == 0) {
                printf("[+] cmd=0x%x read='%s' resp_len=%zu: ",
                       read_cmds[j], read_targets[i], resp_len);
                // Check if response contains file content
                for (size_t k = 0; k < (resp_len < 64 ? resp_len : 64); k++)
                    printf("%02x ", resp[k]);
                printf("\n");
                // Try ASCII
                for (size_t k = 8; k < resp_len && k < 128; k++)
                    if (resp[k] >= 0x20 && resp[k] < 0x7f)
                        putchar(resp[k]);
                printf("\n");
            }
        }
    }

    return 0;
}
```

### PoC 2: If Arbitrary File Write is Found

```c
// Write Magisk su binary to /system/xbin/su (if remount possible)
// OR write adb_keys to gain authorized adb as root
// OR write to /data/adb/post-fs-data.d/ for persistence

const char *adb_pubkey = "YOUR_ADB_PUBKEY_HERE\n";
send_cmd(WRITE_CMD_ID, "/data/misc/adb/adb_keys",
         strlen("/data/misc/adb/adb_keys"), resp, &resp_len);
```

---

## 10. Immediate Next Steps

### Step 1: Ghidra Analysis (30-60 min)

1. Load `vendor_extracted/bin/GateServiced` into Ghidra
2. Find `main()` → trace socket setup
3. Find `cmd_dispatch()` → map the complete command table
4. Trace each command handler's data flow (client input → file ops?)

### Step 2: On-Device Testing (requires ADB shell)

1. Port-forward: `adb forward tcp:18341 tcp:8341`
2. Run `gate_fuzzer_v3.py` against the service
3. Use `logcat -s GATE` to see server-side error messages
4. Correlate responses with Ghidra findings

### Step 3: TA Loading Attack (if Step 1 reveals path control)

1. Create minimal "TA" binary (just ELF with valid header)
2. Push to `/data/local/tmp/`
3. Attempt to load via GateServiced protocol
4. Watch logcat for `"Load app %s failed: %d"` with our path

### Step 4: Alternative — `qttestservice` Property Trigger

1. Check if shell can set `vendor.qttestservice`: `setprop vendor.qttestservice enable`
2. If SELinux allows → reverse the binary for test commands
3. If blocked → try via `RokidSysConfig` confused deputy

---

## Appendix A: All Root Services in Firmware

| Service           | RC File                                        | User | Group           | Special                           |
| ----------------- | ---------------------------------------------- | ---- | --------------- | --------------------------------- |
| GateServiced      | init.gateserviced.rc                           | root | root system     | TCP 8341, disabled→boot_completed |
| poweropt-service  | poweropt-service.rc                            | root | system readproc | HAL class                         |
| qttestservice     | qttestservice.rc                               | root | media           | disabled, property-triggered      |
| qguard            | qguard.rc                                      | root | system readproc | CAP_KILL, late_start              |
| thermal-engine-v2 | init_thermal-engine-v2.rc                      | root | —               | Large binary (3.4MB)              |
| qseecomd          | qseecomd.rc                                    | root | —               | QSEE daemon                       |
| boot@1.2          | android.hardware.boot@1.2-service.rc           | root | —               | HAL                               |
| thermal@2.0       | android.hardware.thermal@2.0-service.qti-v2.rc | root | —               | HAL                               |
| usb.gadget@1.2    | android.hardware.usb.gadget@1.2-service-qti.rc | root | —               | HAL                               |
| usb@1.2           | android.hardware.usb@1.2-service-qti.rc        | root | —               | HAL                               |
| powerstateutility | powerstateutility.rc                           | root | system          | Qualcomm                          |
| limits-hal        | vendor.qti.hardware.limits-service.rc          | root | —               | HAL                               |
| perf-hal          | vendor.qti.hardware.perf-hal-service.rc        | root | —               | HAL                               |
| rmt_storage       | vendor.qti.rmt_storage.rc                      | root | —               | Remote storage                    |
| tftp_server       | vendor.qti.tftp.rc                             | root | —               | TFTP                              |
| sensors.qti       | vendor.sensors.qti.rc                          | root | —               | Sensors HAL                       |
| vmmgr             | vmmgr.rc                                       | root | system          | VM manager                        |

## Appendix B: SELinux `tee` Domain Full Capability Summary

```
Capabilities: dac_override, chown, setgid, setuid, sys_rawio, sys_admin, block_suspend
File access: tee_device, tee_data_file, system_data_file(read), vendor_persist_*,
             vendor_data_tzstorage_file, block_device(search), vendor_rpmb_device(rw),
             vendor_ssd_block_device(rw), firmware_file(search)
Network: Full TCP (create,bind,listen,accept,connect on any port)
IPC: vndbinder, vndservicemanager, init unix socket
HAL client: graphics_allocator, graphics_composer, qdutils_disp, tui_comm, soter,
            powerstateservice
Properties: vendor_tee_listener_prop (set + read)
```

---

_This research plan is based on static analysis of firmware `RVE01-ota-1.14.006-20260204-150203.zip` extracted `vendor.img` and `system.img`. Dynamic validation on the actual device is the critical next step._
