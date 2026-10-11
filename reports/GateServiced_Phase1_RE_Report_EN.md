# GateServiced — Phase 1 Ghidra-Style Reverse Engineering Report

> 中文版：[GateServiced_Phase1_RE_Report.md](GateServiced_Phase1_RE_Report.md)

> ⚠ **2026-09-13 instruction-level review corrections (see `reports/static_re_gate_codes_and_rescue_2026-09-13.md`)**
>
> 1. **Error-code semantics corrected**: `0x3001` = **empty file (size 0)** rather than "file does not exist"; `0x3003` = `read_file` returned a negative value (not the mmap path);
>    **`0x3004` = `calloc` returned NULL** (not DmabufHeapAlloc); `0x3005` = `CreateDmabufHeapBufferAllocator` returned NULL (not a dma_alloc failure);
>    **`0x300A` is a hard-coded catch-all** (it overwrites the real error code); **`0x300F` does not exist**.
> 2. **§8 primitive #2 "File Existence Oracle" should be withdrawn**: `handle_client` only calls `sendto` when `cmd_dispatch` returns 0,
>    and any non-zero return **sends no bytes at all** (the connection stays open) → the client **cannot see any error code** (consistent with on-device testing).
> 3. **All PLT addresses in §7.2 / §7.3 are shifted by +0x20** (PLT0 is 32 B; the `+32+i*16` used by this project's `analyze_gate*.py` is the correct one).
> 4. **`load_app` is not a function** (it was inlined into `run_load_ta` and is only a log tag); the address annotations at `analyze_gate2.py:262-270` are wrong.

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets, and product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (the single authoritative fact table; in case of conflict, that table decides).


## Executive Summary

**Binary**: `vendor_extracted/bin/GateServiced` (20,304 bytes)  
**Architecture**: AArch64 (ARM64), PIE (ET_DYN)  
**Runtime**: root:root, SELinux `tee` domain  
**Capabilities**: CAP_DAC_OVERRIDE, CAP_SETUID, CAP_SYS_ADMIN, CAP_SYS_RAWIO  
**Network**: TCP 0.0.0.0:8341 (no authentication, no encryption)  
**Purpose**: QSEE TrustZone TA (Trusted Application) proxy daemon  
**Internal Codename**: "GATE CA" (Client Application for QSEE Gate)

---

## 1. Complete Function Map

| Address Range   | Size  | Function          | Description                                           |
| --------------- | ----- | ----------------- | ----------------------------------------------------- |
| `0x2000-0x2047` | 72B   | `_start`          | ELF entry → `__libc_init`                             |
| `0x2048-0x20BF` | 120B  | `timing_helper`   | `gettimeofday()`-based elapsed time calculator        |
| `0x20C0-0x23EF` | 816B  | `handle_client`   | Per-connection TCP handler thread                     |
| `0x23F0-0x243F` | 80B   | `handle_sigint`   | SIGINT handler → `exit(0)`                            |
| `0x2440-0x263B` | 508B  | `main`            | Socket server: bind/listen/accept/pthread_create loop |
| `0x263C-0x2807` | 460B  | `cmd_dispatch`    | Command router: LOAD/UNLOAD/EXEC                      |
| `0x2808-0x2953` | 332B  | `get_file_size`   | `fopen→fseek→ftell→fclose` file size query            |
| `0x2954-0x2A6F` | 284B  | `read_file`       | `fopen→fread_unlocked→fclose` binary file reader      |
| `0x2A70-0x2E4B` | 988B  | `run_exec_cmd_ta` | DMA shared memory TA command execution                |
| `0x2E4C-0x3353` | 1288B | `run_load_ta`     | TA binary loading into QSEE via TZCom                 |
| `0x3354-0x346B` | 280B  | `run_unload_ta`   | TA unloading from QSEE                                |
| `0x346C-0x35CF` | 356B  | `dma_alloc`       | DmabufHeap allocation + mmap                          |
| `0x35D0-0x367B` | 172B  | `dma_unalloc`     | munmap + FreeDmabufHeapBufferAllocator                |

**Source**: Debug symbols extracted from `.gnu_debugdata` (XZ-compressed mini-ELF, 1032 bytes)

---

## 2. Wire Protocol Specification

### 2.1 Transport

- **TCP** on port **8341** (hardcoded at `0x24BC`: `MOVK W8, #0x9520, LSL #16`)
- Bound to **0.0.0.0** (all interfaces)
- **No authentication** — any local process can connect
- **No TLS/encryption** — plaintext TCP

### 2.2 Request Frame Format

```
+------------------+---------------------------+
| cmd_id (4B LE)   | payload (0-20476 bytes)   |
+------------------+---------------------------+
```

- **Maximum frame**: 20,480 bytes (`0x5000`) — hardcoded buffer size
- **Minimum frame**: 4 bytes (cmd_id only, no payload)
- Packets ≤ 3 bytes are rejected with error log `"cmd "` (at `0x22A4`)
- `recvfrom()` at `0x2280` reads entire frame as single recv

### 2.3 Response Frame Format

```
+------------------------------------------+
| response_data (0-20480 bytes)            |
+------------------------------------------+
```

- For LOAD/UNLOAD: ASCII string (`"load ta succeed"` / `"unload ta succeed"`)
- For EXEC_CMD: Raw binary response from TA via DMA buffer
- `sendto()` at `0x2350` sends response with actual `output_len`

### 2.4 Command ID Table

| cmd_id       | Command   | Handler             | Payload                                  | Response                            |
| ------------ | --------- | ------------------- | ---------------------------------------- | ----------------------------------- |
| `0x00010003` | LOAD_TA   | `run_load_ta()`     | TA file path (null-terminated, max 128B) | `"load ta succeed"` or error code   |
| `0x00010004` | UNLOAD_TA | `run_unload_ta()`   | None required                            | `"unload ta succeed"` or error code |
| Any other    | EXEC_CMD  | `run_exec_cmd_ta()` | TA command payload (raw binary)          | TA response data or error code      |

**Encoding**: cmd_id is constructed via:

```
0x10003: MOV W8, #0x3; MOVK W8, #0x1, LSL #16    @ 0x268C-0x2690
0x10004: MOV W9, #0x4; MOVK W9, #0x1, LSL #16    @ 0x265C-0x2660
```

---

## 3. Annotated Pseudo-Code

### 3.1 `handle_client(void *arg)` — TCP Handler Thread

```c
void *handle_client(void *arg) {
    int client_fd = *(int*)arg;  // @ 0x20FC: LDR W19, [X0]
    char input_buf[0x5000];       // @ SP+0x520
    char output_buf[0x5000];      // @ SP+0x20
    size_t output_len;            // @ SP+0x8

    // Initialize string pointers for logging
    // "wait for client data, client_fd:%d" @ 0x2148
    // "INFO" @ 0x214C
    // "handle_client" @ 0x2150

    goto recv_loop;  // B 0x2220 @ 0x2164

process_data:  // @ 0x2168
    uint32_t cmd_id = *(uint32_t*)input_buf;  // LDR W28 @ 0x216C

    LOG_INFO("Received, cmd_id: %x, rsv_len:%zu", cmd_id, recv_len);
    // @ 0x2180 string, 0x2194 BL log_print

    gettimeofday(&start_time);  // @ 0x21A8
    output_len = 0;             // @ 0x2160

    int ret = cmd_dispatch(cmd_id,           // W0 = cmd_id
                           input_buf + 4,     // X1 = payload (skip cmd_id)
                           recv_len - 4,      // X2 = payload_len
                           output_buf,        // X3 = output buffer
                           0x5000,            // W4 = max output size
                           &output_len);      // X5 = output length ptr
    // BL cmd_dispatch @ 0x21E0

    if (ret != 0) {
        LOG_ERR("cmd_dispatch failed, ret:%x", ret);
        // @ 0x2200 string, "ERRO" @ 0x2204
        goto recv_loop;
    }

    // Success path @ 0x22BC
    gettimeofday(&end_time);
    uint64_t elapsed = compute_elapsed(&start_time, &end_time);
    LOG_INFO("cmd_id = %x, cmd_dispatch, times = %ld", cmd_id);
    // @ 0x22F8 string

    LOG_INFO("Send: %s, send_len:%zu", output_buf, output_len);
    // @ 0x2324 string

    sendto(client_fd, output_buf, output_len, 0x5000, 0, 0);
    // BL __sendto_chk @ 0x2350

    if (sendto_failed) {
        LOG_ERR("send failed");
        goto error_handler;  // @ 0x2378
    }
    // TBZ W0, #31, 0x2220 @ 0x2364 — check sign bit for error

recv_loop:  // @ 0x2220
    LOG_INFO("wait for client data, client_fd:%d", client_fd);
    memset(output_buf, 0, 0x5000);  // @ 0x224C
    memset(input_buf, 0, 0x5000);   // @ 0x2260

    ssize_t recv_len = recvfrom(client_fd, input_buf, 0x5000, 0, NULL, NULL);
    // BL recvfrom @ 0x2280

    if (recv_len == 0) goto disconnect;  // CBZ @ 0x2284 → 0x2380
    if (recv_len > 3)  goto process_data; // CMP X0,#3; B.HI @ 0x228C-0x2290

    LOG_ERR("cmd [too short]");  // @ 0x22A4
    goto recv_loop;              // B 0x2220 @ 0x22B8

disconnect:  // @ 0x2380
    LOG_INFO("client disconnected");  // @ 0x238C
    close(client_fd);  // BL close @ 0x23AC
    // stack canary check @ 0x23BC-0x23EC
    return;
}
```

### 3.2 `cmd_dispatch()` — Command Router

```c
int cmd_dispatch(uint32_t cmd_id,     // W0
                 void *input_buf,      // X1 → X22
                 size_t input_len,     // X2 → X21
                 void *output_buf,     // X3 → X20
                 size_t max_out,       // W4
                 size_t *output_len)   // X5 → X19
{
    // Frame: 208 bytes (SUB SP, #0xD0 @ 0x263C)
    // Stack canary loaded @ 0x2654-0x2658

    if (cmd_id == 0x10004) {  // CMP W0, W9 @ 0x266C; B.EQ 0x2704 @ 0x2688
        // ─── UNLOAD TA ───
        LOG_INFO("unload ta called");  // @ 0x2710 string
        int ret = run_unload_ta();     // BL @ 0x272C

        char *msg = "unload ta succeed";  // @ 0x2734 string
        size_t msg_len = strlen(msg);     // BL __strlen_chk @ 0x2744
        *output_len = msg_len;            // STR @ 0x2754
        strncpy(output_buf, msg, msg_len); // BL __strncpy_chk2 @ 0x27CC
        return ret;
    }

    if (cmd_id != 0x10003) {  // CMP W0, W8 @ 0x269C; B.NE 0x275C @ 0x26A0
        // ─── EXEC CMD (forward to TA) ───
        // @ 0x275C-0x276C
        return run_exec_cmd_ta(cmd_id, input_buf, input_len,
                               output_buf, output_len);
        // BL run_exec_cmd_ta @ 0x276C
    }

    // ─── LOAD TA ───
    LOG_INFO("load ta called");  // @ 0x26B0 string

    if (input_len >= 128) {  // CMP X21, #0x80 @ 0x26CC; B.CC 0x2778 @ 0x26D0
        LOG_ERR("data_len exceeds maximum length for ta_name");  // @ 0x26E0
        return 0x3002;  // INVALID_PARAMETER @ 0x26FC
    }

    char ta_name[128];
    strncpy(ta_name, input_buf, 128);  // BL __strncpy_chk @ 0x278C
    ta_name[input_len] = '\0';         // null terminate @ 0x2794

    int ret = run_load_ta(ta_name);    // BL @ 0x2798

    char *msg = "load ta succeed";     // @ 0x27A0 string
    size_t msg_len = strlen(msg);      // BL __strlen_chk @ 0x27B0
    *output_len = msg_len;
    strncpy(output_buf, msg, msg_len); // BL __strncpy_chk2 @ 0x27CC
    return ret;
}
```

### 3.3 `run_exec_cmd_ta()` — TA Command Execution via Shared Memory

```c
// Internal name: "run_shared_memory_example" (ISMCIGate pattern)
int run_exec_cmd_ta(uint32_t cmd_id,    // W0 → W25
                    void *input_buf,     // X1 → X24
                    size_t input_len,    // X2 → X23
                    void *output_buf,    // X3 → X22
                    size_t *output_len)  // X5 → X19
{
    // Frame: 160 bytes (SUB SP, #0xA0 @ 0x2A70)

    LOG_INFO("cmd_id = %x, input_len = %zu, output_...", cmd_id, input_len);
    // @ 0x2ABC string ref "run_exec_cmd_ta" @ 0x2AC4

    // ──── Pre-checks ────
    uint8_t loaded_flag = *(uint8_t*)(g_state + 0x3D8);  // @ 0x2AEC
    if (!loaded_flag) {
        LOG_ERR("TA is not already loaded.");  // @ 0x2B6C string
        return 0x300C;  // TA_NOT_LOADED @ 0x2B88
    }

    void *app_obj = *(void**)(g_state + 0x410);  // @ 0x2AF8
    if (app_obj == NULL) {
        LOG_ERR("appObj is NULL!");  // @ 0x2B9C string
        return 0x300D;  // TA_DEAD @ 0x2BB8
    }

    // ──── DMA Buffer Allocation ────
    void *allocator = dup(???);  // BL dup @ 0x2B0C (fd duplication)
    if (allocator == NULL) {
        LOG_ERR("Failed to create DMA buffer allocator");  // @ 0x2BCC
        return 0x3005;  // ALLOC_FAILED @ 0x2BE8
    }

    int ret = dma_alloc(allocator, output_len_ptr, &dma_info);  // BL @ 0x2B28
    if (ret != 0) {
        LOG_ERR("dma_alloc failed with return value =%d", ret);  // @ 0x2B40
        goto cleanup;
    }

    // ──── Prepare DMA Request Buffer ────
    uint8_t *dma_buf = g_dma_ptr;  // LDR X8, [X27, #0x3D0] @ 0x2BF4

    dma_buf[0] = 0x55;                    // STRB W9 @ 0x2C04 — REQUEST MAGIC
    *(uint32_t*)(dma_buf + 1) = cmd_id;   // STR W25 @ 0x2C0C
    *(uint64_t*)(dma_buf + 5) = input_len; // STR X23 @ 0x2C14
    memcpy(dma_buf + 0x0D, input_buf, input_len); // BL memcpy @ 0x2C20

    // ──── TrustZone IPC ────
    int dup_fd = dup(dma_fd);                      // BL dup @ 0x2C28 (approx)
    void *fd_obj = TZCom_getFdObject(dup_fd);      // BL @ 0x2C30 (approx)

    // ... vtable setup ...

    if (vtable_call_failed) {  // @ 0x2C34 CBZ → setup shared mem mapping
        // Call QSEE: ISMCIGate_sharedMemoryExample
        timing_helper(&ts);  // BL @ 0x2DA0
        ret = vtable->sharedMemoryExample(fd_obj, 1, 0x100);  // BLR X11 @ 0x2DC4

        if (ret != 0) {
            LOG_ERR("ISMCIGate_sharedMemoryExample failed %d", ret);  // @ 0x2DDC
            goto cleanup;
        }

        timing_helper(&ts);  // BL @ 0x2DF4
    }

    // ──── Validate TA Response ────
    uint8_t magic = dma_buf[0];  // LDRB W6, [X8, #0x0] @ 0x2DFC

    if (magic != 0xAA) {  // CMP W6, #0xAA @ 0x2E00; B.NE 0x2E24 @ 0x2E04
        LOG_ERR("received magicNumTA failed, magicNumTA=%x", magic);
        // @ 0x2E30 string
        goto cleanup;
    }

    // ──── Extract Response & Return to Client ────
    size_t resp_len = *(size_t*)(dma_buf + ???);  // @ 0x2E08
    *output_len = resp_len;                        // STR @ 0x2E10
    memcpy(output_buf, dma_buf + 0x09, resp_len);  // @ 0x2E1C
    // Note: response data starts at DMA offset 0x09

    if (*output_len == 0) {
        LOG_ERR("Returned data had a length of zero!");  // @ 0x2D10
        return 0x300E;  // RESP_INVALID @ 0x2D2C
    }

cleanup:
    dma_unalloc(allocator, ...);  // BL @ 0x2C78
    // vtable cleanup calls
    return ret;
}
```

### 3.4 `run_load_ta()` — TA Binary Loading into QSEE

```c
int run_load_ta(const char *ta_path)  // X0 → X21
{
    // Frame: 1072 bytes (SUB SP, #0x430 @ 0x2E68)
    // ~ 1288 bytes total function

    // ──── Already loaded check ────
    uint8_t loaded = *(uint8_t*)(g_state + 0x3D8);  // LDRB @ 0x2E7C
    if (loaded == 1) {  // CMP W8, #1 @ 0x2E80
        LOG_INFO("TA is already loaded.");  // @ 0x2E94 string
        return 0;  // success, already loaded
    }

    // ──── NULL check ────
    if (ta_path == NULL) {  // CBZ X0 @ 0x2EBC
        LOG_ERR("TA appPath is NULL!");  // string from rodata 0x1276
        return 0x300B;  // INVALID_APP @ 0x2F48
    }

    // ──── Establish TZCom Client ────
    void *client_env = TZCom_getClientEnvObject(g_tz_state);
    // BL @ 0x2ECC
    if (client_env == NULL) {
        LOG_ERR("Failed to obtain clientenv from TZCom");  // @ 0x2EF8
        return error;
    }

    // ──── Get Apploader Object ────
    int ret = vtable->getApploader(client_env, ...);  // BLR X8 @ 0x2F78
    if (ret != 0) {
        LOG_ERR("Failed to get apploader object with %d", ret);  // @ 0x2F98
        goto cleanup;
    }

    // ──── Read TA Binary from Disk ────
    ssize_t file_size = get_file_size(ta_path);  // BL @ 0x2FC8
    if (file_size == 0) {
        return 0x3001;  // GENERAL_ERROR @ 0x30B4 (file empty or not found)
    }

    void *dma_buf = DmabufHeapAlloc(file_size, 1);  // BL @ 0x2FE0 (approx)
    if (dma_buf == NULL) {
        LOG_ERR("Malloc failed while allocating memory");  // @ 0x30C8
        // internal func name: "load_app" @ 0x30D0
        return 0x3004;  // ALLOC_FAILED @ 0x30E4
    }

    ret = read_file(ta_path, dma_buf, file_size);  // BL @ 0x2FF8
    if (ret < 0) {
        return 0x3003;  // NOT_SUPPORTED @ 0x30F4
    }

    // ──── Load TA into QSEE ────
    void *mapped = mmap(dma_buf, ...);  // BL mmap @ 0x3048
    // Load command: 0x1001 @ 0x3018
    ret = vtable->loadApp(client_env, 0, TA_data, file_size); // BLR X8 @ 0x3028

    // ──── Get Application Object ────
    ret = vtable->getAppObject(...); // Multiple BLR calls @ 0x306C, 0x31B0, etc.
    if (ret != 0) {
        LOG_ERR("Getting the application object failed");  // @ 0x308C
        return error;
    }

    // ──── Verify TA — Get Version ────
    // Calls run_exec_cmd_ta internally to query TA info
    ret = run_exec_cmd_ta(query_cmd, ...);  // BL @ 0x32E8
    if (ret != 0) {
        // internal func: "get_ta_version" @ 0x3308
        LOG_ERR("dtxGetAppletInfo fail: %08x", ret);  // @ 0x3300
        goto error_cleanup;
    }

    LOG_INFO("ta version = %s", ta_version);  // @ 0x332C

    g_loaded_flag = 1;  // mark TA as loaded
    return 0;
}
```

### 3.5 `read_file()` — Binary File Reader

```c
int read_file(const char *path,   // X0 → X19
              void *buffer,        // X1 → X20
              size_t size)         // X2 → X22
{
    // Frame: 96 bytes
    FILE *f = fopen(path, "rb");  // @ 0x2980; mode string at rodata 0x1664

    if (f == NULL) {  // CBZ @ 0x2984 → 0x2A08
        int err = errno;
        char *errstr = strerror(err);
        LOG_ERR("Failed to open file %s: %s (%d)", path, errstr, err);
        // @ 0x2A2C string
        return -1;  // @ 0x2A50
    }

    size_t read = fread_unlocked(buffer, 1, size, f);  // @ 0x299C

    if (read != size) {  // CMP X0, X20 @ 0x29A0
        int err = errno;
        char *errstr = strerror(err);
        LOG_ERR("Failed reading the file %s: %zu vs %zu bytes: %s (%d)",
                path, read, size, errstr, err);
        // @ 0x29CC string
        return -1;
    }

    fclose(f);  // @ 0x2A00
    return 0;   // success
}
```

### 3.6 `get_file_size()` — File Size Query

```c
ssize_t get_file_size(const char *path)  // X0 → X19
{
    // Frame: 64 bytes
    FILE *f = fopen(path, "rb");  // mode at rodata 0x1664

    if (f == NULL) {
        LOG_ERR("Failed to open file %s: %s (%d)", path, strerror(errno), errno);
        // @ 0x28B8 string
        return -1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {  // @ 0x283C
        LOG_ERR("Failed seeking in file %s: %s (%d)", path, strerror(errno), errno);
        // @ 0x2868 string
        fclose(f);
        return -1;
    }

    long size = ftell(f);
    if (size < 0) {  // @ 0x2918
        LOG_ERR("Failed telling size of file %s: %s (%d)", path, strerror(errno), errno);
        // @ 0x293C string
        fclose(f);
        return -1;
    }

    fclose(f);
    return size;
}
```

---

## 4. DMA Buffer Layout (run_exec_cmd_ta)

### 4.1 Request Structure (GateServiced → QSEE TA)

```
Offset  Size  Content
──────  ────  ────────────────────────────────
0x00    1     Magic: 0x55 (REQUEST marker)
0x01    4     cmd_id (32-bit LE, from client)
0x05    8     input_len (64-bit)
0x0D    var   payload data (memcpy from client input)
```

### 4.2 Response Structure (QSEE TA → GateServiced)

```
Offset  Size  Content
──────  ────  ────────────────────────────────
0x00    1     Magic: 0xAA (RESPONSE marker, success)
0x01    ?     response length
0x09    var   response data (copied to client output buffer)
```

### 4.3 Magic Number Analysis

- **Request**: `0x55` = `01010101` binary
- **Response**: `0xAA` = `10101010` binary
- **Relationship**: Complementary bit pattern (XOR = `0xFF`)
- **Purpose**: Handshake verification between GateServiced and TA
- **Check location**: `CMP W6, #0xAA` at `0x2E00`
- **Failure log**: `"received magicNumTA failed, magicNumTA=%x"` at `0x2E30`

### 4.4 DMA Heap Configuration

- **Heap name**: `"qcom,qseecom-ta"` (string at rodata `0xFD2`, referenced at `0x3494`)
- **Allocator**: `CreateDmabufHeapBufferAllocator` → `DmabufHeapAlloc` → `mmap`
- **Sync**: `DmabufHeapCpuSyncStart` / `DmabufHeapCpuSyncEnd` for cache coherency
- **TZ Integration**: `TZCom_getFdObject` wraps DMA fd, `TZCom_getClientEnvObject` for client session
- **QSEE Interface**: ISMCIGate (inferred from `"ISMCIGate_sharedMemoryExample"` string at `0x2DDC`)

---

## 5. Global State Layout (at page 0x5000)

| Offset  | Type     | Description                           |
| ------- | -------- | ------------------------------------- |
| `0x3C0` | uint64_t | Timestamp storage (timing helper)     |
| `0x3D0` | void\*   | DMA buffer pointer (shared memory)    |
| `0x3D8` | uint8_t  | TA loaded flag (0=unloaded, 1=loaded) |
| `0x3E0` | void\*   | TZCom client environment object       |
| `0x400` | void\*   | QSEE app object vtable                |
| `0x410` | void\*   | QSEE app object instance              |

---

## 6. Error Code Table

| Code     | Name              | Location | Function        | Trigger                                 |
| -------- | ----------------- | -------- | --------------- | --------------------------------------- |
| `0x3001` | GENERAL_ERROR     | `0x30B4` | run_load_ta     | File size = 0 (file not found or empty) |
| `0x3002` | INVALID_PARAMETER | `0x26FC` | cmd_dispatch    | ta_name > 128 bytes                     |
| `0x3003` | NOT_SUPPORTED     | `0x30F4` | run_load_ta     | read_file failed (mmap error path)      |
| `0x3004` | ALLOC_FAILED      | `0x30E4` | run_load_ta     | DmabufHeapAlloc returned NULL           |
| `0x3005` | DMA_ALLOC_FAILED  | `0x2BE8` | run_exec_cmd_ta | dma_alloc failed                        |
| `0x300A` | UNKNOWN           | `0x3178` | run_load_ta     | Internal error (2 locations)            |
| `0x300B` | INVALID_APP       | `0x2F48` | run_load_ta     | ta_path is NULL                         |
| `0x300C` | TA_NOT_LOADED     | `0x2B88` | run_exec_cmd_ta | Loaded flag = 0                         |
| `0x300D` | TA_DEAD           | `0x2BB8` | run_exec_cmd_ta | appObj is NULL                          |
| `0x300E` | RESP_INVALID      | `0x2D2C` | run_exec_cmd_ta | Response data length = 0                |

---

## 7. Import Table — Attack Surface Assessment

### 7.1 Dangerous Imports (NONE)

```
❌ system()    — NOT IMPORTED
❌ exec*()     — NOT IMPORTED
❌ popen()     — NOT IMPORTED
❌ dlopen()    — NOT IMPORTED
❌ fork()      — NOT IMPORTED
❌ ptrace()    — NOT IMPORTED
```

**Conclusion**: Command injection is IMPOSSIBLE through this binary.

### 7.2 File I/O Imports

| Function         | PLT Addr | Called From              | Purpose                    |
| ---------------- | -------- | ------------------------ | -------------------------- |
| `fopen`          | `0x37C0` | read_file, get_file_size | Open TA binary (mode "rb") |
| `fread_unlocked` | `0x3820` | read_file                | Read TA binary content     |
| `fseek`          | `0x37D0` | get_file_size            | Seek to EOF for size       |
| `ftell`          | `0x3800` | get_file_size            | Get file position (=size)  |
| `fclose`         | `0x3810` | read_file, get_file_size | Close file handle          |

### 7.3 Defenses Present

- **Stack Canary**: `__stack_chk_fail` at PLT `0x36A0` — all functions have canary checks
- **PIE**: Binary is ET_DYN (position-independent), ASLR applies
- **strncpy**: Bounded copies with `__strncpy_chk`, `__strncpy_chk2`
- **Fixed buffers**: 0x5000-byte input/output buffers, never dynamically sized from client

---

## 8. Exploitation Primitives Rating

| #   | Primitive                             | Rating                            | Feasibility                                                                                        | Notes                                                                                                   |
| --- | ------------------------------------- | --------------------------------- | -------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| 1   | **Arbitrary File Read (as root)**     | 🟡 MEDIUM                         | File is read but contents go to QSEE, NOT back to client                                           | Only via LOAD_TA (0x10003). Path is client-controlled. File contents sent to TrustZone, not TCP client. |
| 2   | **File Existence Oracle**             | 🟢 HIGH                           | Differentiate error codes: 0x3001 (file empty/missing) vs QSEE rejection (file valid but not a TA) | Non-destructive reconnaissance. Can map filesystem as root.                                             |
| 3   | **Path Traversal**                    | 🟢 HIGH                           | No path sanitization in cmd_dispatch. `strncpy(ta_name, input, 128)` with no `..` filtering.       | Paths like `../../../../etc/passwd`, `/data/data/com.xxx/db` all accepted.                              |
| 4   | **TA Proxy (Command Forwarding)**     | 🔴 CRITICAL (if useful TA exists) | Any cmd_id != 0x10003/0x10004 forwarded to loaded TA. Response sent back to client.                | If TA has file read/write/exec capabilities, full exploitation possible.                                |
| 5   | **Buffer Overflow**                   | 🔵 LOW                            | Stack canaries on all functions. Fixed 0x5000 buffers. `strncpy` bounded.                          | Would need canary leak first (side-channel).                                                            |
| 6   | **Command Injection**                 | ⚫ NONE                           | No system/exec/popen/dlopen imports.                                                               | Impossible.                                                                                             |
| 7   | **Information Leak (Error Messages)** | 🟡 MEDIUM                         | Error codes return to client but NOT log strings.                                                  | Error code differentiation reveals state (loaded/unloaded/dead)                                         |
| 8   | **State Manipulation**                | 🟡 MEDIUM                         | LOAD overwrites global state. UNLOAD clears it. Race conditions possible.                          | Two concurrent connections could race LOAD vs EXEC_CMD.                                                 |

---

## 9. Attack Chains

### 9.1 Primary Chain: TA Discovery → Load → Proxy Exploitation

```
┌──────────────┐     ┌──────────────────┐     ┌─────────────────────┐
│ 1. ENUMERATE │     │ 2. LOAD TARGET   │     │ 3. EXPLOIT VIA TA   │
│    TA FILES  │────▶│    TA BINARY     │────▶│    COMMAND PROXY    │
└──────────────┘     └──────────────────┘     └─────────────────────┘
  cmd=0x10003          cmd=0x10003              cmd=<TA-specific>
  path=<probe>         path=<valid_ta>          payload=<TA cmd data>
  analyze error        expect "load ta          response = TA output
  codes                succeed"                 back to TCP client!
```

**Step 1**: Use LOAD_TA with path traversal to probe for TA files on the filesystem

- Error 0x3001 → file doesn't exist or is empty
- Any other error → file exists (read succeeded, but QSEE rejected non-TA content)

**Step 2**: Load a legitimate TA binary found on the device

- Common locations: `/vendor/firmware/*.mbn`, `/vendor/lib/rfsa/adsp/*`

**Step 3**: Send TA-specific commands via EXEC_CMD

- The response from the TA is forwarded back to the TCP client
- If the TA has file I/O or privileged operations, we can leverage them

### 9.2 Secondary Chain: File Existence Oracle for Reconnaissance

```
┌───────────────┐     ┌────────────────┐     ┌──────────────────┐
│ DICTIONARY OF │     │  SEND 0x10003  │     │  ANALYZE ERROR   │
│ INTERESTING   │────▶│  WITH EACH     │────▶│  CODE PATTERNS   │
│ PATHS         │     │  PATH          │     │  (0x3001 vs other│
└───────────────┘     └────────────────┘     └──────────────────┘
```

**Value**: Map device filesystem layout as root, discover:

- Installed apps and their data
- Configuration files
- Other exploitable binaries
- Available TA binaries for Step 1 of primary chain

### 9.3 Race Condition Chain (Theoretical)

```
Thread A: LOAD_TA(malicious_path)    ──┐
                                        ├── Race condition on g_loaded_flag
Thread B: EXEC_CMD(cmd, data)        ──┘
```

If thread A's LOAD partially completes (flag set but object not initialized) and thread B's EXEC_CMD reads stale pointers → possible use-after-free or null-deref crash.

---

## 10. Key Strings in `.rodata` (Indexed)

| Offset   | String                        | Used By                               |
| -------- | ----------------------------- | ------------------------------------- |
| `0x000D` | `"INFO"`                      | All functions (log level tag)         |
| `0x00A0` | `"run_load_ta"`               | run_load_ta                           |
| `0x050F` | `"handle_client"`             | handle_client                         |
| `0x06A2` | `"read_file"`                 | read_file                             |
| `0x06AC` | `"get_ta_version"`            | run_load_ta (post-load verify)        |
| `0x08AF` | `"ERRO"`                      | All functions (error log tag)         |
| `0x08B4` | `"run_shared_memory_example"` | run_exec_cmd_ta                       |
| `0x0866` | `"load ta succeed"`           | cmd_dispatch                          |
| `0x0264` | `"unload ta succeed"`         | cmd_dispatch                          |
| `0x0EC5` | `"run_exec_cmd_ta"`           | run_exec_cmd_ta                       |
| `0x0F07` | `"cmd_dispatch"`              | cmd_dispatch                          |
| `0x0FD2` | `"qcom,qseecom-ta"`           | dma_alloc (DMA heap name)             |
| `0x1012` | `"get_file_size"`             | get_file_size                         |
| `0x1276` | `"TA appPath is NULL!"`       | run_load_ta                           |
| `0x1664` | `"rb"`                        | fopen mode (read_file, get_file_size) |

---

## 11. Methodology Notes

### 11.1 Analysis Tools Used

- **Custom Python ELF parsers** (`analyze_gate.py`, `analyze_gate2.py`, `analyze_gate3.py`, `extract_exec_ta.py`)
- No Ghidra/IDA available — all analysis performed via:
  - ELF header/section/segment parsing
  - AArch64 instruction decoding (manual bit-field extraction)
  - PLT/GOT cross-reference mapping
  - `.gnu_debugdata` decompression (LZMA/XZ → mini-ELF with symbol table)
  - ADRP+ADD string resolution across `.rodata`

### 11.2 Confidence Levels

- **Function boundaries**: HIGH (from debug symbols)
- **cmd_id values**: HIGH (verified via MOV+MOVK decoding)
- **DMA buffer layout**: MEDIUM (offsets inferred from STRB/STR/ADD patterns)
- **Protocol format**: HIGH (confirmed by buffer sizes and CMP checks)
- **Error codes**: HIGH (direct MOV immediate values)
- **Vtable call targets**: LOW (BLR indirect, need runtime analysis)

---

## 12. Next Steps (→ Phase 2 Dynamic Testing)

1. **Validate protocol format** — Send raw TCP packets with correct cmd_id encoding
2. **Enumerate TA files** — Path traversal scanner on `/vendor/firmware/` tree
3. **File existence oracle** — Build path dictionary, differentiate error responses
4. **Load known TA** — Find and load a legitimate TA binary
5. **TA command fuzzing** — Send various cmd_ids to loaded TA, capture responses
6. **Race condition testing** — Concurrent LOAD + EXEC_CMD threads
7. **Monitor crash behavior** — `logcat | grep "GATE CA"` for anomaly detection
