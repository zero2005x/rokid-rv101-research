# Rokid AR Glasses (RV101) Privilege Escalation Vulnerability Research Report

> 中文版：[COMPREHENSIVE_PRIVESC_REPORT.md](COMPREHENSIVE_PRIVESC_REPORT.md)

> ⚠ **Historical document (not the current state)**: this text records the situation at the time; the project has since changed significantly (root acquired, engine switched to runtime probing, phone UI redone, i18n 193 keys).
> For the current state see `README.md`, `MANIFEST.md`, `docs/deploy.md`.


## Device Information

| Item    | Value                             |
| ------- | --------------------------------- |
| Model   | Rokid RV101 (NEO LUNA-V2)         |
| Platform| Qualcomm Technologies             |
| GPU     | Adreno 621v1 (Chip ID: 0x6020100) |
| Kernel  | 5.10.209-perf-gb80a8319d11b       |
| Android | 12                                |
| SELinux | Enforcing (u:r:shell:s0)          |
| Seccomp | Disabled                          |
| User    | shell (UID 2000)                  |

## Attack Surface Summary

### 1. KGSL (GPU Driver) - /dev/kgsl-3d0 ✅

**Status**: accessible (mode 0666)

**Confirmed vulnerabilities**:

1. **Use-After-Free (UAF)**
   - mmap remains readable and writable after the fd is closed
   - But GPU addresses are not reused (even after 5000 rounds of spray)
   - **Conclusion**: the UAF exists but is not exploitable

2. **Out-of-Bounds Read (OOB)**
   - Negative offsets can be used to read other GPU buffers
   - No kernel pointers found

3. **Shadow Buffer**
   - GPU addr: 0xeffff000, size: 32KB
   - Can be mmap'd (read-only)
   - Contains only timer values; no kernel pointer leak

### 2. I2C - /dev/i2c-1 ✅ **Significant finding**

**Status**: accessible (mode 0666)

**Discovered devices**:
| Address | Type | Writable | Details |
|------|------|------|------|
| 0x08 | PMIC (power management) | ✅ | ID=0x22, can control power rails |
| 0x30 | Display controller | ✅ | 256 readable registers |
| 0x3F | RTC/sensor | ✅ | Contains time data |
| 0x64 | Battery management IC | ✅ | Reports 31% charge |

**Attack potential**:

- **PMIC control**: may cause power anomalies or a system reset
- **Display controller**: may have debug/backdoor functionality
- **Battery IC**: may allow forging charge data

### 3. SPI - /dev/rt600_spidev ✅

**Status**: accessible (mode 0666)

- SPI Mode: 0x03
- Max Speed: 12 MHz
- Bits: 8
- All flash commands return zero (the device may not be connected or may require initialization)

### 4. Serial - /dev/ttyHS0 ✅

**Status**: writable

- High-speed serial port
- Baud rate: B4096
- No pending data
- **May be connected to an external coprocessor**

### 5. Binder ⚠️

**Status**: accessible but restricted

- BC_ENTER_LOOPER causes the process to block (expected behavior)
- Most system services require privileges

### 6. DMA-BUF Heaps ❌

**Status**: permission denied

- Heaps such as qcom,system and qcom,qseecom are inaccessible

### 7. ADSPRPC (DSP RPC) ❌

**Status**: read-only

- /dev/adsprpc-smd is readable but cannot be opened RDWR
- ioctl returns ENOTTY

### 8. QSEE/QCE ❌

**Status**: permission denied

- /dev/qseecom and /dev/qce are inaccessible

## Possible Attack Vectors

### High priority

1. **I2C memory leak**
   - EEPROM reads return non-EEPROM data
   - Possibly a driver vulnerability
   - Further research recommended

2. **KGSL race condition**
   - The UAF exists but is hard to exploit
   - Consider cross-process spray or timing attacks

### Medium priority

3. **SPI flash attack**
   - rt600_spidev is fully controllable
   - If the device can be initialized, external flash may be readable and writable

4. **Serial coprocessor**
   - ttyHS0 may be connected to the RT600 coprocessor
   - Sending specific commands may trigger a vulnerability

### Low priority

5. **Property Service**
   - persist.\* properties can be set
   - ro._ or ctl._ properties cannot be set

## Ruled-Out Attack Vectors

| Vector                     | Reason                                    |
| -------------------------- | ----------------------------------------- |
| Dirty Pipe (CVE-2022-0847) | Patched                                   |
| SUID binaries              | No SUID executables                       |
| DRM/Graphics               | Blocked by SELinux                        |
| ION Allocator              | Device does not exist                     |
| Kernel Symbols             | KASLR enabled, addresses read back as zero|

## Artifact List

```
/data/local/tmp/privesc_scanner      # privilege scanner
/data/local/tmp/kgsl_deep_research   # KGSL deep research
/data/local/tmp/kgsl_uaf_v2          # UAF exploitation attempt
/data/local/tmp/kgsl_massive_spray   # large-scale spray
/data/local/tmp/kgsl_shadow_v2       # shadow analysis
/data/local/tmp/i2c_attack           # I2C attack
/data/local/tmp/spi_serial_attack    # SPI/Serial attack
/data/local/tmp/dsp_ion_attack       # DSP/ION attack
/data/local/tmp/adsprpc_dmabuf       # ADSPRPC exploration
```

## Recommended Next Steps

1. **Deeper I2C research**: analyze the suspicious data returned; a driver buffer overflow may be present
2. **RT600 coprocessor**: study the SPI/Serial communication protocol and try to interact with the coprocessor
3. **Vendor HAL analysis**: decompile the vendor HAL libraries looking for vulnerabilities
4. **Kernel module analysis**: study Qualcomm/Rokid-specific kernel modules

## Conclusion

No directly exploitable privilege escalation vulnerability has been found so far. The main obstacles:

1. SELinux enforcing mode restricts most operations
2. KASLR prevents kernel address leaks
3. The KGSL UAF exists, but the GPU address manager does not reuse freed addresses

The suspicious behavior of the I2C driver deserves further study and may be the most promising attack vector.
