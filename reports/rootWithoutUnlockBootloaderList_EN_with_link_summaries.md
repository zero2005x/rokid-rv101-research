# Recent Vulnerabilities Used to Obtain Root Without Unlocking the Bootloader

> Purpose: security research, risk tracking, and defensive assessment.
>
> Warning: Do not use this material for unauthorized testing or attacks.

## 1. Overview of Known Vulnerabilities and Attack Chains

### 1.1 Vulnerability 1: CVE-2025-21479 (Qualcomm Adreno GPU Micronode Memory Corruption)

This vulnerability exists in the Qualcomm Adreno GPU micronode command-processing path. Public descriptions indicate that a specially crafted command sequence may trigger unauthorized command execution and memory corruption. Public GitHub PoCs include:

- https://github.com/zhuowei/cheese
- https://github.com/sarabpal-dev/cheese-cake

If the vulnerability can be successfully chained into a system privilege-escalation path, it may lead to temporary root access. Actual impact still depends on the SoC, GPU generation, firmware version, and patch status.

In the attack-chain taxonomy used by this report, CVE-2025-21479 and the ABL Cmdline Injection issue described later have something in common: both may serve as an initial entry point for **weakening SELinux enforcing restrictions** or putting the system into **SELinux permissive** mode.

Once SELinux mandatory access control is no longer enforced, additional local privilege-escalation techniques may become available, such as Xiaomi `IMQSNative` / `MQSAS` service calls or the isolated-service / isolated-process privilege-escalation class represented by Magica.

Community test report (other models still require verification):

- Source: Coolapk user `@羊了个羊了个羊了个羊`
- Link: https://www.coolapk.com/feed/70655251?s=NTFjZmEwYzkyN2NkOGUyZzY5YzYxOTM4ega1601
- Key point: the post describes testing `cheese` on a Redmi Note 12 Turbo (tagged `#红米Note12Turbo`) and reports behavior associated with temporary root.
- Note: this is a single community report. It should be archived as a **feasibility signal**, not as broad confirmation; cross-validation across additional models and firmware versions is still required.

---

### 1.2 Vulnerability 2: ABL Cmdline Injection (fastboot OEM / ABL Kernel-Command-Line Injection Chain)

The core issue in this class of chains is incomplete validation of certain `fastboot oem` parameters by Qualcomm ABL (Android Bootloader), allowing untrusted input to propagate into the kernel command line. Public discussions have demonstrated injection of boot arguments such as `androidboot.selinux=permissive`, weakening mandatory security restrictions after boot.

The corresponding Qualcomm patch describes the issue directly as:

`Fix propagation of untrusted input into kernel cmdline`

Therefore, this chain is usually not the final objective by itself. Instead, it can act as a prerequisite for subsequent privilege escalation, temporary root, or operations that ultimately avoid the normal bootloader-unlock path.

#### 1.2.1 Additional Known Variants and Chained Use

In addition to the commonly discussed form:

```bash
fastboot oem set-gpu-preemption 0 androidboot.selinux=permissive
```

a similar form has also been discussed:

```bash
fastboot oem set-hw-fence-value 0 androidboot.selinux=permissive
```

The underlying issue is similar: a `fastboot oem` command that should accept only a numeric value may pass additional input into the kernel command line, allowing `androidboot.selinux=permissive` to be injected so that Android boots in **SELinux permissive** mode rather than enforcing mode.

Known observations:

- The `set-gpu-preemption` path can be used to disable SELinux enforcement. It is **Qualcomm-specific**, and a patch commit is publicly available.
- `set-hw-fence-value` is another similar variant and has also been patched. Public discussion characterizes this as an older class of bug that may theoretically exist across a broader range of Qualcomm SoCs.
- Community version-distribution observations currently show more exploitability reports on **HyperOS 2 / HyperOS 3**. Other versions require more samples and independent verification.

On **Xiaomi devices**, public discussions also mention combining this with the following system-service call:

```bash
service call miui.mqsas.IMQSNative 21 i32 1 s16 "命令" i32 1 s16 "参数列表" s16 "输出路径" i32 600
```

The important point is that, if the corresponding service interface is reachable under the required conditions, it may allow arbitrary commands to execute with **root privileges**.

The sequence matters. CVE-2025-21479, Qualcomm ABL cmdline injection, or another method must first cause SELinux enforcing to fail or transition the system to `SELinux permissive`; only then may later local privilege-escalation techniques be chainable into a complete path.

Two commonly discussed post-permissive branches are:

1. **Xiaomi `IMQSNative` / `MQSAS` service path**: after SELinux becomes permissive, access to the relevant service interface may allow commands to execute as root.
2. **Magica / isolated-service path**: the Magica README describes a privilege-escalation PoC for Android 10+ when seccomp is disabled. Public discussion also describes isolated-service / isolated-process techniques as possible post-permissive escalation paths.

Using Xiaomi `IMQSNative` / `MQSAS` as an example, the chain can be summarized as:

1. Use an ABL-class issue to inject `androidboot.selinux=permissive`.
2. Boot the system in **SELinux permissive** mode.
3. Invoke Xiaomi's `miui.mqsas.IMQSNative` service.
4. Obtain **arbitrary command execution as root**.

Under those conditions, a complete root-acquisition chain may be formed.

With a Magica / isolated-service branch, the emphasis is not on a specific Xiaomi service. Instead, after **SELinux permissive** or another security restriction has weakened the execution boundary, isolated-service / isolated-process behavior and system-service boundaries may become useful in subsequent privilege escalation.

Accordingly, Sections 1.1 and 1.2 can be viewed as possible initial mechanisms for neutralizing SELinux restrictions, while `IMQSNative` and Magica represent different post-permissive escalation branches.

References:

- `set-gpu-preemption` patch:
  https://git.codelinaro.org/clo/la/abl/tianocore/edk2/-/commit/fb8e864254cdc370670233e3cb73a2b18ff33c9f
- `set-hw-fence-value` patch:
  https://git.codelinaro.org/clo/la/abl/tianocore/edk2/-/commit/78297e8cfe091fc59c42fc33d3490e2008910fe2
- Magica:
  https://github.com/vvb2060/Magica
- Discussion sources:
  https://t.me/vvb2060_Channel/17
  https://t.me/vvb2060_Channel/19

> Notes:
>
> - `ABL Cmdline Injection` is a technical label used by this report to group fastboot OEM / ABL input-validation flaws that can cause kernel-command-line injection.
> - The source report states that Xiaomi fixed related issues in its February 2026 security patches.

### 1.3 Vulnerability 3: GBL / UEFI Secure-Boot-Chain Class (`gbl_root_canoe`)

Reference project:

- https://github.com/superturtlee/gbl_root_canoe

`gbl_root_canoe` is a recently published GBL / UEFI / ABL research project targeting newer Qualcomm platforms. According to its public description, it does not merely modify Android userspace; it interacts with the **GBL / UEFI / ABL / efisp** boot-chain layer.

The main risks can be summarized as follows:

- The focus is on newer Qualcomm platforms, particularly devices associated with **Snapdragon 8 Elite Gen 5 / Snapdragon 8 Gen 5**.
- The relevant execution point is in the boot chain before Android userspace starts.
- Replacement, patching, or repackaging of boot-chain components may alter the device's boot state, verification flow, or fastboot behavior.
- Public descriptions mention `lockmode` / `unlockmode`, suggesting that the tooling may support a "fake locked" presentation under certain conditions.
- This class differs from ordinary Android userspace privilege escalation and is closer to a **bootloader / secure-boot-chain bypass** risk.

Platforms mentioned in public material are listed in Section 3.4, "Devices Pending Verification."

> Note: "Potentially affected" here means that a device or platform appears in public projects or community research. It does not mean that every device unit or firmware build has been confirmed exploitable.

---

### 1.4 Vulnerability 4: MTK Preloader-Class Chain (OPPO / Realme / OnePlus)

Reference project:

- https://github.com/Shocked-Cat/oppo-mtk-fastboot-unlock

This research primarily targets **MediaTek platforms**, especially OPlus-family devices such as OPPO, Realme, and OnePlus. The public project describes a method based on modifying a factory preloader and writing that preloader with mtkclient in order to expose fastboot access or continue toward bootloader unlocking.

Main risks:

- The attack surface is in the **MediaTek boot chain / Preloader** stage.
- This is not an Android-userspace vulnerability.
- Altering Preloader behavior may change whether a device can enter fastboot or proceed through an unlock path.
- On some devices, boot-chain modification or unlocking may alter the secure-boot state.
- Whether this can ultimately produce root without using the official bootloader-unlock process depends on later image verification, AVB / vbmeta behavior, Preloader encryption, and vendor-specific checks.

> Notes:
>
> - Exploitability on MTK devices is highly vendor-specific. Even devices using the same MediaTek SoC may behave very differently because of different Preloaders, DA/auth policies, and OEM customizations.
> - The source report states that on some OPPO / Realme / OnePlus MTK devices, later Android 13-era firmware/security updates introduced encrypted or restricted DA handling, making authorized service tooling or older firmware relevant to this research path.

---

### 1.5 Vulnerability 5: MediaTek Secure-Boot-Chain Bypass (`fenrir`)

Reference project:

- https://github.com/R0rt1z2/fenrir

`fenrir` is a public PoC targeting the MediaTek secure boot chain. It originally focused on the **Nothing Phone (2a)** / **CMF Phone 1**, and the upstream README later expanded support to multiple MediaTek devices from Nothing, CMF, Lenovo, Tecno, Zinwa, Redmi, and POCO.

Key points from the public description:

- The vulnerability is in the **MediaTek secure boot chain**.
- Under specific conditions, a boot component is not properly verified.
- The PoC can break the secure-boot chain after Preloader execution.
- The README states that it can achieve **EL3 code execution**.
- The README also describes lock-state spoofing so that a device in a non-standard state may present itself as locked.
- At the time represented by the source document, upstream listed 12 supported device codenames; "supported" did not mean every regional variant, OTA, or firmware build had been independently tested.
- The Releases page primarily contained prebuilt assets for `Pacman` and `PacmanPro`; other devices could require firmware-specific builds or adaptation.
- The author listed the Vivo X80 Pro as known affected, but not as part of the formal support list.

The device/support table appears in Section 3.2.

---

### 1.6 Vulnerability 6: Dirty Pipe (CVE-2022-0847)

Dirty Pipe (CVE-2022-0847) is a publicly exploited local privilege-escalation vulnerability in the Linux kernel.

It involves pipe-buffer / page-cache behavior and, under the necessary conditions, can allow an attacker to modify cached content associated with files that should otherwise be read-only, potentially leading to privilege escalation.

References:

- NVD: https://nvd.nist.gov/vuln/detail/CVE-2022-0847
- Original Dirty Pipe explanation: https://dirtypipe.cm4all.com/
- Android research: https://github.com/polygraphene/DirtyPipe-Android
- Android research: https://github.com/tiann/DirtyPipeRoot

### 1.7 Vulnerability 7: GhostLock (CVE-2026-43499)

GhostLock is a Use-After-Free local privilege-escalation vulnerability in the Linux kernel `rtmutex` / `futex_requeue()` path. The flaw occurs in proxy-lock rollback: under certain conditions, `remove_waiter()` operates on `current` instead of the actual `waiter::task`, potentially leaving an uncleared `pi_blocked_on` dangling pointer that can become exploitable as a UAF.

The vulnerability has progressed from a complete Linux exploit to device-specific Android adaptations. Public projects from NebuSec / CyberMeowfia include both triggering/exploitation code, while later work includes Android 16, Xiaomi 17-series, and Samsung Galaxy firmware-specific ports/tooling.

#### 1.7.1 Android Impact Assessment

- Common Android Linux branches such as 5.10, 6.1, 6.6, and 6.12 may or may not be affected depending on the actual code and backport status.
- The source report lists patched stable-branch boundaries including `6.1.175`, `6.6.140`, `6.12.86`, `6.18.27`, and `7.0.4`. OEM kernels may backport an equivalent fix earlier, so a version string alone is not sufficient.
- A public Xiaomi 17 Pro Max (`popsicle`, Android 16 / kernel 6.12) adaptation exists. Other projects provide frameworks that generate device-specific payload material from `boot.img`, kernel layout, and device profiles.
- The issue may provide temporary root, kernel control, or a first stage for later LKM-root work, but it **does not by itself unlock the bootloader, bypass AVB, or create persistent root**.

#### 1.7.2 Public Status

| Item | Status |
|---|---|
| Vulnerability type | Linux kernel UAF / Local Privilege Escalation |
| Linux exploit | Public |
| Android device ports | Public; includes Xiaomi and Samsung firmware-specific examples |
| Universal? | No; kernel, firmware, layout, and profile must match |
| Direct bootloader unlock? | No |
| Report classification | Kernel LPE / Temporary Root / High-priority Android tracking |

References:

- NVD: https://nvd.nist.gov/vuln/detail/CVE-2026-43499
- Original public exploit: https://github.com/NebuSec/CyberMeowfia/tree/main/IonStack/CVE-2026-43499/exploit
- Xiaomi 17-series Android port: https://github.com/x-spy/CVE-2026-43499-popsicle
- Android arm64 adaptation framework: https://github.com/Linuxoid-cn/CVE-2026-43499-Poc-Analysis
- Samsung Galaxy adaptation/tooling: https://github.com/BuSung-dev/Root-My-Galaxy

### 1.8 Vulnerability 8: Bad Epoll (CVE-2026-46242)

Bad Epoll is a race-condition Use-After-Free in the Linux kernel `epoll` / `eventpoll` subsystem. Public research shows that a low-privileged local process can turn this UAF into kernel local privilege escalation on affected Linux kernels. The vulnerable behavior was introduced by commit `58c9b016e128` and fixed by commit `a6dc643c6931`.

#### 1.8.1 Android Impact Assessment

- The issue was introduced in Linux v6.4, so Android devices based on newer 6.6 / 6.12 kernels that have not received an equivalent fix are candidate targets.
- The author reports triggering the UAF on a Pixel 10 v6.6+ kernel; at the point reflected in the source document, a complete Android root exploit was still under development.
- Pixel 8 and other devices based on an unmodified v6.1 baseline are not affected by this upstream issue unless the introducing change was separately backported.
- OEMs can selectively backport both vulnerable and fixing commits, so actual kernel source/commits and firmware must be checked rather than relying only on Android version or `uname -r`.

#### 1.8.2 Public Status

| Item | Status |
|---|---|
| Vulnerability type | Linux kernel epoll UAF / Local Privilege Escalation |
| Linux exploit | Public; complete chain exists for kernelCTF targets |
| Android trigger | Publicly demonstrated on Pixel 10 v6.6+ |
| Complete Android root exploit | Not yet publicly complete in the referenced material |
| Direct bootloader unlock? | No |
| Report classification | Kernel LPE / High-priority Android verification target |

References:

- Bad Epoll research and PoC: https://github.com/J-jaeyoung/bad-epoll
- NVD: https://nvd.nist.gov/vuln/detail/CVE-2026-46242
- oss-security: https://www.openwall.com/lists/oss-security/2026/07/08/13

### 1.9 Vulnerability 9: CVE-2026-21385 (Qualcomm Display / Graphics Memory Corruption)

CVE-2026-21385 is an integer-overflow / memory-corruption issue in Qualcomm Display / Graphics components related to alignment handling during memory allocation. Qualcomm assigns CVSS v3.1 7.8 (High), with local attack vector, low privileges, and no user interaction. The March 2026 Android Security Bulletin classifies it as a high-severity Qualcomm Display issue.

The risk is particularly important because the Android bulletin reports evidence of **limited, targeted exploitation**, and CISA added it to the Known Exploited Vulnerabilities (KEV) catalog. It is therefore more than a theoretical memory-corruption issue and warrants high-priority patch tracking.

#### 1.9.1 Android Impact Assessment

- Affected scope must be determined from Qualcomm's chipset list, OEM firmware, and the actual included fixes. It should not be generalized to every Qualcomm / Adreno device.
- A public trigger, crash, or observed overflow only demonstrates reachability of the vulnerable path. It does not automatically imply controlled memory corruption or a complete Android root exploit.
- At the time of the source report, no broadly reproducible, cross-device public Android root PoC had been confirmed.
- If a stable exploit primitive can be constructed for a specific device, it could potentially act as an app-to-kernel, temporary-root, or chain entry point; it still does not directly unlock the bootloader.

#### 1.9.2 Public Status

| Item | Status |
|---|---|
| Vulnerability type | Qualcomm Display / Graphics Memory Corruption |
| Official severity | High (CVSS 7.8) |
| Known exploitation | Android bulletin reports limited targeted exploitation; CISA KEV-listed |
| Complete public Android root PoC | Not confirmed |
| Direct bootloader unlock? | No |
| Report classification | Qualcomm Graphics attack surface / Known exploited / Very high-priority tracking |

References:

- Android Security Bulletin (2026-03): https://source.android.com/docs/security/bulletin/2026/2026-03-01
- Qualcomm March 2026 Security Bulletin: https://docs.qualcomm.com/product/publicresources/securitybulletin/march-2026-bulletin.html
- NVD: https://nvd.nist.gov/vuln/detail/CVE-2026-21385

## 2. Potential Follow-on Attack Techniques

This section summarizes attack techniques frequently discussed alongside the public research above.

They may appear at different stages: after a device state has been modified, during local App/system-service checks, while network verification data is in transit, or when a remote service evaluates device trust.

The focus is on:

- Which layer a technique operates at.
- Which decision or security signal it attempts to bypass or interfere with.
- Which prerequisites are commonly required.
- What effect it may have on the device, App, or remote service.

> Reading the flow sections: each subsection describes a distinct attack surface and is not necessarily part of one linear chain from `2.1` through `2.7`. A prerequisite, patch-state, or trust-boundary condition can stop a path at any point.

Sections `2.5` and `2.6` specifically describe **post-root boot-chain manipulation**: another vulnerability first provides root/kernel control, which must then be converted into write access to GPT, LK, or another boot-chain partition before bootloader state can be affected. These sections do not describe how first-stage root is obtained.

### 2.1 Local Bypass: Device-Side Trust-State Spoofing

A local bypass means that an attacker already has sufficient control over the device and then attempts to change or hide locally observable state so that Apps, system services, or integrity-checking components cannot correctly determine whether the device has been modified.

Common signals include:

- Whether the bootloader is locked.
- Whether Verified Boot / AVB is normal.
- Whether the device uses a trusted boot key.
- Whether SELinux remains enforcing.
- Whether root, injection frameworks, suspicious mount points, or modified system properties exist.
- Whether Key Attestation / Play Integrity results match the device's real state.

The main objective is to create a discrepancy between the true state and what local checks observe. For example, a modified device may still appear normal, locked, or trusted to an App or service.

#### 2.1.1 Flow Illustration

#### 2.1.2 Risk Classification

| Category | Description |
|---|---|
| Type | Post-root / Post-bootchain-compromise Local Bypass |
| Attack location | Local device, App-check flow, system-service return values |
| Primary purpose | Spoof local device state and reduce detection by Apps/services |
| Related mechanisms | Boot state, AVB, SELinux, system properties, root detection, local Key Attestation / Play Integrity call results |
| Common prerequisite | Ability to affect local checking, system properties, return values, or execution environment |
| Main risk | Local checks may be misled into treating a modified device as trusted |

### 2.2 Relay / Response-Substitution Attacks: Interference with Key-Attestation Verification

Reference project:

- https://github.com/vocolboy/RemoteKeyAttestation

> Positioning note: the project above is research/demonstration work around RKA / RKP and Play Integrity. It is **not** a generic PoC showing that an arbitrary network MITM can modify hardware attestation. RKA / RKP are key/certificate provisioning mechanisms. The relevant security question is whether the server correctly validates signatures, challenges, certificate chains, revocation state, and binding when a result is relayed, substituted, or replayed.

This research focuses on trust boundaries in Remote Key Attestation / Key Attestation verification.

An attacker may attempt to interfere with data handling between the App, verification service, and attestation result so that the verifier receives a substituted, replayed, forwarded, or otherwise mismatched result.

Relevant server-side bindings include:

- Whether challenge / nonce values are replay-resistant.
- Whether the result is bound to the current request, account, device, and App identity.
- Whether the certificate chain and hardware trust source are correctly validated.
- Whether request/response traffic between App and remote service can be inserted into or substituted.
- Whether a service merely trusts a client-side verdict rather than validating the attestation statement on the server.

Accordingly, a relay/MITM-style issue should be treated as a **verification-flow problem**: the goal is to mislead the remote service about the real trust state of a device.

#### 2.2.1 Flow Illustration

#### 2.2.2 Risk Classification

| Category | Description |
|---|---|
| Type | Attestation Relay / Response Substitution / Verification Bypass |
| Attack location | Requests, responses, or processing between App and remote verifier |
| Primary purpose | Interfere with remote integrity verification and device-trust decisions |
| Related mechanisms | Remote Key Attestation, Key Attestation, RKP, Play Integrity, challenge/nonce, certificate-chain validation |
| Common prerequisite | Incomplete server validation of signatures, challenge, freshness, chain/revocation, or request/device/App/account binding |
| Main risk | Remote service may be misled into treating an untrusted device as trusted |

### 2.3 ADB / Agent-Assisted Forensics: Acquiring Application Data

A common mobile-forensics objective is to obtain application data, system records, media, account traces, communication records, and other artifacts under lawful authorization or in a controlled test environment.

Android's official documentation describes ADB as a command-line tool for communicating with a device, installing/debugging Apps, and obtaining a Unix shell. In forensic workflows, it is therefore often used as a base channel for device connection, logical acquisition, agent deployment, or data export.

The main limitation is Android's application sandbox and storage isolation. Official Android documentation states that an App's internal storage is not accessible to other Apps by default, and on Android 10+ these locations are encrypted. As a result, ordinary ADB, MTP, or a non-privileged agent can generally access only a subset of user data, shared/external storage, media, exportable App data, or UI-layer information.

Forensic tools may therefore use different acquisition levels depending on device state and authorization, for example:

- Standard ADB or agent-based backup.
- Installing a helper App for logical extraction.
- Logical / physical backup from an already rooted device.
- SoC- or vendor-specific dump/acquisition methods.
- Screen capture, manual collection, or App-supported export when filesystem access is unavailable.

ADB / agent-assisted forensics does not inherently mean exploitation. However, when combined with an ADB privilege-escalation flaw, system-service vulnerability, SELinux bypass, or changed boot-chain state, it may evolve from ordinary logical acquisition into a path capable of accessing private App data or a larger portion of the filesystem.

#### 2.3.1 Flow Illustration

#### 2.3.2 Risk Classification

| Category | Description |
|---|---|
| Type | ADB / Agent-assisted Forensic Acquisition |
| Attack/acquisition location | ADB channel, device agent, App-data and filesystem access boundary |
| Primary purpose | Acquire App artifacts, user data, system records, media, or forensic evidence |
| Related mechanisms | ADB, adbd, Android App sandbox, internal storage, logical extraction, agent-based extraction |
| Common prerequisite | Unlocked/accessible device, USB debugging, user authorization, agent installation, or higher privilege |
| Main risk | Combined with privilege escalation, sandbox-protected App data may become accessible |

References:

- NIST SP 800-101 Rev. 1: https://csrc.nist.gov/pubs/sp/800/101/r1/final
- Android Debug Bridge: https://developer.android.com/tools/adb
- Android app-specific storage: https://developer.android.com/training/data-storage/app-specific
- Magnet Acquire: https://www.magnetforensics.com/resources/magnet-acquire/
- Belkasoft X: https://belkasoft.com/x
- Oxygen Forensics Android Agent: https://www.oxygenforensics.com/technical-resources/android-agent/

### 2.4 Qualcomm GBL Bootloader-Unlock Chain

Recent Qualcomm GBL bootloader-unlock research focuses on the loading and validation boundary among **Qualcomm ABL / GBL / UEFI / efisp** in newer Android boot chains.

Public reporting states that on some Android 16 / Qualcomm platforms, Qualcomm ABL attempts to load a GBL-related UEFI application from the `efisp` partition. The weakness described in public research is that the loading path may establish that content is a UEFI application without sufficiently verifying that it is the trusted, OEM-expected GBL component. If an attacker already has the ability to write `efisp`, a custom UEFI application may therefore be able to execute during the bootloader stage.

This matters because the execution point is **before Android userspace starts**. Unexpected code execution at this layer may influence bootloader lock state, critical-unlock state, fastboot behavior, Verified Boot decisions, or later boot behavior.

Commonly discussed components of a chained path include:

- **GBL / efisp loading-validation gap**: incomplete authenticity/identity validation when ABL loads a UEFI application from `efisp`.
- **Ability to write `efisp`**: normally a separate prerequisite obtained through another vulnerability, system service, root, SELinux permissive plus another primitive, or a vendor-specific channel.
- **fastboot OEM / ABL cmdline injection**: sometimes combined to put SELinux into permissive mode or reduce restrictions before later writes.
- **Vendor-service/system-level capability**: e.g., Xiaomi HyperOS / MQSAS-related capabilities in specific reported chains.
- **Bootloader-state modification**: a custom UEFI application may alter `is_unlocked`, `is_unlocked_critical`, or equivalent state.

A critical distinction is that `SELinux permissive` only disables SELinux policy enforcement. It does **not** automatically provide root, block-device capabilities, or `efisp` write access. A working chain still needs root, a privileged vendor service, or another partition-write primitive.

The central risk is bypassing OEM bootloader-unlock policy and pushing devices that are normally difficult or impossible to officially unlock into a flashable, modifiable, or non-standard trust state. For forensics/research, this can introduce lower-layer acquisition paths; for defense, boot-chain validation, `efisp` write controls, fastboot OEM argument validation, and vendor service permissions all need review.

#### 2.4.1 Flow Illustration

#### 2.4.2 Risk Classification

| Category | Description |
|---|---|
| Type | Qualcomm GBL / ABL Boot Chain Unlock |
| Attack location | Qualcomm ABL, GBL, UEFI app, `efisp`, bootloader lock state |
| Primary purpose | Bypass OEM bootloader-unlock restrictions and alter boot/flash state |
| Related mechanisms | GBL, UEFI, efisp, ABL, fastboot OEM, SELinux permissive, Verified Boot / AVB |
| Common prerequisite | Android 16 / GBL architecture, pre-existing ability to write `efisp`, or another chain that supplies the needed privilege |
| Main risk | Boot-chain influence before Android userspace, potentially leading to unlock/fake-lock/root/integrity-verdict consequences |

#### 2.4.3 Current Observations

- Public attention is centered on **Snapdragon 8 Elite Gen 5** and Android 16 / GBL devices.
- Public reporting mentions unlock cases involving Xiaomi 17-series, Redmi K90 Pro Max, and POCO F8 Ultra devices.
- Devices such as Samsung phones that use their own bootloader implementation require separate analysis; conclusions from Qualcomm ABL / GBL cannot simply be applied to them.

References:

- `gbl_root_canoe`: https://github.com/superturtlee/gbl_root_canoe
- Android Authority: https://www.androidauthority.com/qualcomm-snapdragon-8-elite-gbl-exploit-bootloader-unlock-3648651/
- Android GBL overview: https://source.android.com/docs/core/architecture/bootloader/generic-bootloader
- GBL fastboot: https://android.googlesource.com/platform/bootable/libbootloader/+/refs/heads/main/gbl/docs/gbl_fastboot.md
- Android bootloader locking/unlocking: https://source.android.com/docs/core/architecture/bootloader/locking_unlocking

### 2.5 Qualcomm AVB / `NO_AVB` Bootloader-State Vulnerability

Reference projects:

- https://github.com/atlas4381/qualcomm_avb_exploit_poc
- https://github.com/kasnria001/qualcomm_noavb_exploit_common
- Mi8G3-Unlocker: https://github.com/Linuxoid-cn/Mi8G3-Unlocker
- Mi8G2-Unlocker: https://github.com/Linuxoid-cn/Mi8G2-Unlocker
- Mi8sG4-Unlocker: https://github.com/Linuxoid-cn/Mi8sG4-Unlocker
- Mi8sG3orMi7pG3-Unlocker: https://github.com/Linuxoid-cn/Mi8sG3orMi7pG3-Unlocker
- Mi8E-Unlocker: https://github.com/Linuxoid-cn/Mi8E-Unlocker

This section is categorized as a follow-on attack technique because an attacker who has already obtained root/kernel control through a kernel, GPU, system-service, or other vulnerability—and can convert that access into raw block-device, GPT, or boot-chain-partition writes—may be able to extend first-stage temporary root into a change to the bootloader's device state. Similar write capability could also come from EDL, a service channel, or another low-level flaw. **The Qualcomm AVB / NO_AVB issue is not, by itself, a first-stage root vulnerability.**

Unlike Section `2.6`, where a patched LK changes the unlock policy directly, Section `2.5` ultimately relies on the interaction among `NO_AVB`, the Keymaster milestone, and RPMB device state. Some practical tools temporarily flash a factory/engineering ABL first, using its engineering boot/fastboot/flashing capabilities to modify GPT and start a second-stage payload. In such chains, ABL replacement is an enabling medium, not the final primitive that rewrites lock state.

Root identity alone does not guarantee the ability to rewrite GPT, `vbmeta`, ABL, or another protected partition. SELinux, Linux capabilities, read-only block mapping, UFS write protection, anti-rollback, and OEM storage services can all block writes. The later AVB/RPMB chain is viable only when root/kernel control can be converted into a real low-level write primitive.

This research focuses on how Qualcomm ABL determines **Android Verified Boot 2.0 (AVB2)** state and how that decision affects Keymaster TA behavior, RPMB device state, and bootloader-unlock state.

Public PoC material states that some vulnerable ABL builds check at runtime whether GPT contains `vbmeta_a` or `vbmeta` to choose the AVB2 path. If the expected partition is not found, ABL may enter a `NO_AVB` path.

The issue is that the `NO_AVB` path may not set the Keymaster milestone used by the normal AVB2 path. Without that milestone, controls around Keymaster TA device-state read/write operations may be insufficient, allowing an attacker who already has low-level GPT/partition write capability to influence RPMB data related to bootloader state, such as unlock / critical-unlock flags.

The central state-management gap is therefore between **AVB detection, Keymaster milestone state, and RPMB device state**. If the conditions hold, the device may bypass the normal `fastboot oem unlock` or OEM-authorized unlock process and enter an unlocked / critical-unlocked bootloader state.

Publicly discussed key points include:

- **AVB2 detection depends on runtime GPT observations** in vulnerable builds.
- **The `NO_AVB` path lacks the normal milestone constraint**.
- **Keymaster TA device-state boundaries may be insufficient without the milestone**.
- **RPMB bootloader state can consequently be affected**.
- **Fix direction**: make the AVB2 decision a compile-time setting such as `VERIFIED_BOOT_ENABLED`, rather than allowing runtime GPT changes to influence it.

#### 2.5.1 Factory / Engineering ABL, Unlock GPT, and Ennea

Several Linuxoid-cn Xiaomi unlock-assistant projects combine temporary root, factory/engineering ABL, model-specific GPT data, and an `Ennea.img` unlock image into automated workflows. The details vary by platform, but the shared design shown by public READMEs, release layouts, and scripts can be summarized as:

1. Obtain temporary root/kernel control from an existing vulnerability, or obtain ABL/GPT write capability through EDL or another service/maintenance path.
2. When required for the model, temporarily write a matching factory/engineering ABL to expose engineering boot behavior or fastboot flash/boot functions that were otherwise unavailable.
3. Use factory userspace, an existing privileged/root service, or engineering fastboot to write a model-specific `unlockGPT`. This typically changes GPT partition entries so that `vbmeta` / `vbmeta_a` is renamed, hidden, or no longer recognizable to ABL; it is not an ordinary filesystem rename.
4. Use ABL to boot an `Ennea.img` compatible with the SoC. This acts as a second-stage unlock payload and interacts with Keymaster TA / RPMB device-state behavior under `NO_AVB` / unset-milestone conditions.
5. After updating device state, some tools restore the original GPT and production ABL, then check the actual unlock state. Version/slot/GPT/anti-rollback mismatches can lead to boot failure or permanent damage.

Public projects:

| Project | Qualcomm platform | Main resources visible in public package | README-stated scope |
|---|---|---|---|
| `Mi8G3-Unlocker` | Snapdragon 8 Gen 3 | `8650-Ennea.img`, factory images, model-specific unlock GPT; some models include exploit / `su` | Xiaomi 14 / Redmi K70 Pro, K80 / MIX Fold 4, MIX Flip, etc. |
| `Mi8G2-Unlocker` | Snapdragon 8 Gen 2 | `8550-Ennea.img`, factory ABL / GPT, unlock GPT | Xiaomi 13 series, Redmi K60 Pro / K70, Pad 6S Pro, etc.; README states a patch-date restriction |
| `Mi8sG4-Unlocker` | Snapdragon 8s Gen 4 | `8735-Ennea.img`, factory GPT, unlock GPT; scripts expect a matching engineering ABL | Redmi Turbo 4 Pro, Xiaomi Civi 5 Pro, Xiaomi Pad 8 |
| `Mi8sG3orMi7pG3-Unlocker` | Snapdragon 8s Gen 3 / 7+ Gen 3 | `8635-Ennea.img`, factory images, unlock GPT; some models include exploit / `su` | Redmi Turbo 3, Xiaomi Civi 4 Pro, Pad 7 / Pad 7 Pro |
| `Mi8E-Unlocker` | Snapdragon 8 Elite | `8750-Ennea.img`, factory ABL / GPT, unlock GPT | Xiaomi 15 series, Redmi K80 Pro / K90, Pad 8 Pro; README states a patch-date restriction |

#### 2.5.2 Flow Illustration

#### 2.5.3 Risk Classification

| Category | Description |
|---|---|
| Type | Qualcomm AVB / `NO_AVB` Device-State Bypass |
| Position in chain | Post-root / Post-kernel-control Bootloader-State Manipulation |
| Attack location | Qualcomm production/factory ABL, GPT, `vbmeta`, Ennea/Recovery payload, Keymaster TA, RPMB device state |
| Primary purpose | Influence bootloader unlock / critical-unlock state and bypass the normal unlock process |
| Related mechanisms | AVB2, `vbmeta`, GPT, factory/engineering ABL, fastboot boot, `NO_AVB`, `KEYMASTER_MILESTONE_CALL`, RPMB, Keymaster TA |
| Directly obtains root? | No; normally assumes temporary root/kernel control or EDL/another low-level write path already exists |
| Common prerequisite | Ability to write matching ABL/GPT or alter `vbmeta` visibility, plus a compatible second-stage environment and Keymaster device-state interface |
| Main risk | Runtime AVB-state detection may be misled by GPT changes, allowing unexpected modification of bootloader state |

#### 2.5.4 Current Observations

- `qualcomm_avb_exploit_poc` states that it was tested on Redmi 14R (`flame`, Snapdragon 4 Gen 2) and may affect other devices using a vulnerable ABL build.
- The source report notes Coolapk discussion of successful unlocks on Snapdragon 8 Gen 2, Snapdragon 8 Gen 3, and Snapdragon 8 Elite devices using related techniques.
- Linuxoid-cn projects package the chain into SoC/model-specific tools; differences include the temporary-root source, factory/engineering ABL, GPT layout, `Ennea.img`, and Keymaster/RPMB adaptation.
- The source report states that one public `Mi8sG4-Unlocker` implementation combines permissive mode, Xiaomi privileged-service write capability, a temporary engineering ABL, engineering fastboot, unlock GPT, `Ennea.img`, and restoration of the original GPT. Other releases may use factory ADB, different root primitives, or EDL and should not be assumed to use identical commands.

References:

- `qualcomm_avb_exploit_poc`: https://github.com/atlas4381/qualcomm_avb_exploit_poc
- `qualcomm_noavb_exploit_common`: https://github.com/kasnria001/qualcomm_noavb_exploit_common
- Patch: https://git.codelinaro.org/clo/la/abl/tianocore/edk2/-/commit/1b2e5f9c4e95db4c74570b828d047e45f9f426d1
- Mi8G3-Unlocker: https://github.com/Linuxoid-cn/Mi8G3-Unlocker
- Mi8G2-Unlocker: https://github.com/Linuxoid-cn/Mi8G2-Unlocker
- Mi8sG4-Unlocker: https://github.com/Linuxoid-cn/Mi8sG4-Unlocker
- Mi8sG3orMi7pG3-Unlocker: https://github.com/Linuxoid-cn/Mi8sG3orMi7pG3-Unlocker
- Mi8E-Unlocker: https://github.com/Linuxoid-cn/Mi8E-Unlocker
- Third-party workflow analysis/risk record: https://mitanyan98.hatenablog.com/entry/2026/06/13/095658

### 2.6 MediaTek LK Certificate Bypass and Bootloader-State Manipulation

Reference projects:

- lkpatcher: https://github.com/R0rt1z2/lkpatcher
- pwnage24mtk: https://github.com/kasnria001/pwnage24mtk
- fenrir: https://github.com/R0rt1z2/fenrir

This section also belongs to the post-root bootloader attack surface. If an attacker has already obtained root/kernel control and can use it to access LK or another boot-chain block device, a modified LK may be written back to extend temporary root into altered unlock state, restoration of a standard fastboot unlock path, or another persistent boot-chain behavior.

Root only solves part of the "can this partition be written?" problem. It does not make an unsigned/modified LK automatically pass MediaTek secure boot. This is the role of the certificate bypass in the chain: **root/kernel control supplies or helps obtain the LK-partition write primitive; LK patching changes unlock behavior; certificate bypass may cause a vulnerable Preloader to accept the modified LK.** Missing any one of these capabilities does not constitute a complete bootloader-unlock chain.

This research focuses on MediaTek Preloader verification of ASN.1 / `cert2` certificates for boot-chain components such as `bl2_ext`, LK, and ATF. Public research states that some V5/V6 certificate parsers may mishandle nested objects or extra hash fields, allowing a modified image to preserve original signature material while causing the verification path to use attacker-controlled partition-header and image hashes.

`lkpatcher` implements two certificate-bypass generation strategies:

- `override`: prepends a hash-override structure before the untouched original certificate.
- `wrap`: corresponds to older V5 / some V6 parsing behavior.

Both are intended to make a modified bootloader partition acceptable to a vulnerable certificate parser.

Passing certificate verification only means a modified image may be accepted by the boot chain. The actual bootloader-unlock effect still comes from the **behavior patched into LK itself**, for example:

- **Directly altering lock-state logic** so LK operates as unlocked or changes how it evaluates `seccfg` / device lock state.
- **Restoring a standard unlock path** by modifying an OEM-disabled/restricted fastboot unlock handler.

Three independent capabilities must therefore be distinguished:

1. **LK patching** changes unlock policy, fastboot behavior, or subsequent image-verification logic.
2. **Certificate bypass** makes a modified LK / boot-chain partition potentially acceptable to a vulnerable MediaTek certificate parser.
3. **Low-level write primitive** installs the modified image. Certificate bypass does not itself provide BROM, Preloader, DA, or fastboot partition-write access.

A real chain still needs a workable low-level write path. This may come from existing root/kernel control, a BROM / Preloader / DA flaw, already-open fastboot, a vendor service channel, or another boot-chain issue. If SELinux, block-device permissions, or hardware write protection still blocks the root context, a generated modified image cannot be installed.

#### 2.6.1 Possible Combination with `fenrir`

`fenrir` exploits a MediaTek boot-chain logic flaw in which `bl2_ext` is not correctly verified under a specific `seccfg` state, gaining EL3 code execution and potentially affecting later image verification and displayed lock state. Certificate bypass addresses a different problem: whether a modified boot-chain component is accepted by the certificate parser.

If the target's certificate parser, partition packaging, `bl2_ext` / LK versions, and write path are compatible, the two may theoretically be combined by:

- Patching LK's unlock / fastboot behavior with `lkpatcher`.
- Using certificate bypass so the modified component is accepted by Preloader.
- Adding `fenrir` where needed to control later verification at `bl2_ext` / EL3 or to produce lock-state spoofing / fake-locked behavior.

This is a **device-specific potential chain**, not a claim that `lkpatcher` and `fenrir` can be directly combined on every MediaTek device. SBC, parser differences, OEM LK customization, anti-rollback, and partition-write restrictions may prevent the chain.

#### 2.6.2 OEM-Specific Unlock State and RPMB

AOSP defines the basic semantics of `LOCKED` / `UNLOCKED`, but OEMs can customize how these states are stored, validated, and synchronized. In addition to `seccfg` read by LK, vendors may use persistent authorization data in TEE/RPMB, device binding, anti-rollback state, and account/server-side unlock records.

Xiaomi's official unlock process includes Xiaomi-account/device binding, waiting periods, and server authorization. Community reverse engineering and device observations for some Xiaomi MediaTek models further suggest that stock LK may check both `seccfg` and device-specific unlock-verification data in RPMB. Changing only `seccfg`, or replacing LK with one that skips the check, may not create exactly the same persistent state as the official unlock flow.

> Note: this dual-check observation comes primarily from community reverse engineering and testing on specific Xiaomi MediaTek models and should not be generalized to every Xiaomi, Qualcomm, or MediaTek device.

#### 2.6.3 Flow Illustration

#### 2.6.4 Risk Classification

| Category | Description |
|---|---|
| Type | MediaTek LK Certificate Verification Bypass / Bootloader-State Manipulation |
| Position in chain | Post-root / Post-kernel-control LK Replacement and Bootloader-State Manipulation |
| Attack location | Preloader cert parser, `bl2_ext`, LK, `seccfg`, RPMB/secure-storage checks, fastboot unlock handler |
| Primary purpose | Allow a modified bootloader component to pass vulnerable verification and then affect unlock state or restore an unlock path |
| Directly obtains root? | No; after boot-chain control/unlock, a modified image still has to be loaded to create persistent root |
| Required conditions | Vulnerable parser, device-specific LK patch, matching firmware, a low-level write primitive, and behavior compatible with OEM persistent state |
| Relationship to fenrir | May be combined on compatible devices for EL3/`bl2_ext` control or lock-state spoofing, but not required or universal |
| Main limitations | V5/V6 certificate formats, SBC, anti-rollback, OEM LK customization, RPMB/TEE state, server authorization, patch state, partition-write restrictions |

#### 2.6.5 Public Status and CVE Mapping

- `lkpatcher` publicly implements `override` / `wrap` certificate-bypass modes and LK patch categories including fastboot, dm-verity, orange state, and red state.
- `pwnage24mtk` describes the ASN.1 certificate-parser issue as similar to `CVE-2023-20696` and discusses different behavior for older V5/V6 versus newer V6 parsers.
- The newer `override` variant should not automatically be labeled `CVE-2023-20696`; the public project does not provide a formal CVE mapping for that newer variant.
- `lkpatcher` still notes that some workflows require prior `seccfg` handling and that SBC devices may not be supported, reinforcing the need for device/firmware-specific verification.

References:

- lkpatcher README: https://github.com/R0rt1z2/lkpatcher
- lkpatcher cert-bypass implementation: https://github.com/R0rt1z2/lkpatcher/blob/master/lkpatcher/cert_bypass.py
- lkpatcher default patch categories: https://github.com/R0rt1z2/lkpatcher/blob/master/patches.json
- pwnage24mtk: https://github.com/kasnria001/pwnage24mtk
- fenrir: https://github.com/R0rt1z2/fenrir
- CVE-2023-20696: https://nvd.nist.gov/vuln/detail/CVE-2023-20696
- AOSP Device State: https://source.android.com/docs/security/features/verifiedboot/device-state
- AOSP Locking and Unlocking the Bootloader: https://source.android.com/docs/core/architecture/bootloader/locking_unlocking
- Xiaomi official bootloader-unlock FAQ: https://www.mi.com/tw/support/faq/details/KA-21069/
- Xiaomi MIUI Security White Paper — Secure Storage: https://trust.mi.com/docs/miui-security-white-paper-global/3/1
- Xiaomi MTK `seccfg` / RPMB community observation: https://4pda.to/forum/index.php?showtopic=721838&st=59800

### 2.7 Post-root Biometric AuthToken Attack: PIN Recovery / CE Bypass

Reference research:

- DARKNAVY — The Biometric AuthToken Heist: https://www.darknavy.org/blog/the_biometric_authtoken_heist/#disclosure

This technique is not used to obtain Android root. It applies **after** the attacker already has Android normal-world root or kernel (EL1) control and then attacks the trust relationship among biometric Trusted Applications (TAs), AuthToken, and KeyMint / Gatekeeper.

Normally, even with root, PIN verification and Credential Encryption (CE) keys remain protected by TEE / TrustZone and cannot simply be read from Android's normal world. DARKNAVY reports that some vendors' fingerprint / face TAs may expose AuthToken-signing capabilities or HMAC keys that should remain in the secure world because of inadequate access control, sensitive logging, or memory-safety vulnerabilities.

If an attacker can obtain or forge a valid AuthToken accepted by KeyMint, the attacker may bypass Gatekeeper's normal online retry/throttling controls and extend existing root privileges into PIN recovery. On some device/firmware combinations, this can also lead to CE decryption risk.

#### 2.7.1 Main Attack Surfaces

The reported paths include:

- A biometric TA signs or returns a valid AuthToken without correctly binding it to a real, fresh biometric event.
- A production trustlet or secure-world log leaks the AuthToken HMAC key.
- Memory corruption in a biometric TA exposes the HMAC key or another token-generation capability.
- KeyMint does not strictly distinguish Gatekeeper AuthTokens from biometric AuthTokens, allowing a token to be accepted in a context where it should not be valid.

#### 2.7.2 Position in the Attack Chain

#### 2.7.3 Public Validation and Adaptation Limitations

DARKNAVY reports examining more than 30 Android devices from 9 manufacturers and completing PIN recovery on 8 devices from 7 vendors. Six of those worked in the Before First Unlock (BFU) state and enabled CE bypass; two applied only in After First Unlock (AFU) conditions.

These findings do not imply a universal cross-device PIN-recovery tool. Exploitability depends heavily on:

- The fingerprint / face TA actually loaded on the device.
- Fingerprint-sensor vendor and sensor SDK.
- Qualcomm, MediaTek, or other TEE implementation.
- How Keymaster / KeyMint validates AuthToken type and fields.
- BFU vs. AFU state.
- Whether vendor/TEE/trustlet firmware has been patched.

Even the same phone model may contain different biometric TAs by region, production batch, or sensor supplier, so each device/firmware combination requires verification.

#### 2.7.4 Risk Classification

| Category | Description |
|---|---|
| Type | Post-root TEE / AuthToken Attack Surface |
| Directly obtains root? | No; root/kernel control is a prerequisite |
| Main impact | PIN recovery, CE-decryption risk, increased risk to authentication-protected operations |
| Components | Biometric TA, Gatekeeper, Keymaster/KeyMint, AuthToken, TEE/TrustZone |
| Universal? | No; TA, sensor, TEE, KeyMint, and firmware-specific |
| Public status | Technical details and multi-device validation are public; no universal cross-model exploit tool is provided |
| Report classification | A post-root path that may extend a "rooted phone" into a "decrypted phone" |

#### 2.7.5 Public Patch Information

The research discloses Samsung `CVE-2025-20987`, `CVE-2025-20988`, and `CVE-2025-20989`, as well as Meizu `CNVD-2025-08623`. Motorola, TECNO, and other unnamed vendors also confirmed and/or patched related issues.

> Note: DARKNAVY's disclosure section and Samsung's official advisory map the vulnerability types for `CVE-2025-20988` / `CVE-2025-20989` differently. This report follows Samsung's official advisory: `CVE-2025-20988` is an out-of-bounds read in a fingerprint trustlet; `CVE-2025-20989` is improper logging / HMAC-key exposure.

References:

- DARKNAVY full research: https://www.darknavy.org/blog/the_biometric_authtoken_heist/
- Samsung Mobile Security (SMR Jun-2025 Release 1): https://security.samsungmobile.com/securityUpdate.smsb?month=06&year=2025

## 3. Device Impact List

### 3.1 Xiaomi / Redmi / POCO

> Notes:
>
> - This table only represents models and vulnerability states tested by the source author; omission does not mean another model/version is unaffected.
> - In the source table, **Tested** means the test successfully changed the SELinux policy to Permissive. **Untested** does not mean unaffected; it only means independent verification has not been completed.

| Codename | Device | Platform | Last-tested Android | Last-tested security patch | Vulnerability / CVE | Status | Notes |
|---|---|---|---|---|---|---|---|
| cupid | Xiaomi 12 | Snapdragon 8 Gen 1 | Android 15 | 2025-11-01 | CVE-2025-21479 | Tested, unsuccessful | |
| zeus | Xiaomi 12 Pro | Snapdragon 8 Gen 1 | N/A | N/A | CVE-2025-21479 | Untested | |
| mayfly | Xiaomi 12S | Snapdragon 8+ Gen 1 | N/A | N/A | CVE-2025-21479 | Untested | |
| unicorn | Xiaomi 12S Pro | Snapdragon 8+ Gen 1 | N/A | N/A | CVE-2025-21479 | Untested | |
| thor | Xiaomi 12S Ultra | Snapdragon 8+ Gen 1 | N/A | N/A | CVE-2025-21479 | Untested | |
| fuxi | Xiaomi 13 | Snapdragon 8 Gen 2 | N/A | N/A | ABL Cmdline Injection | Untested | |
| nuwa | Xiaomi 13 Pro | Snapdragon 8 Gen 2 | N/A | N/A | ABL Cmdline Injection | Untested | |
| ishtar | Xiaomi 13 Ultra | Snapdragon 8 Gen 2 | N/A | N/A | ABL Cmdline Injection | Untested | |
| houji | Xiaomi 14 | Snapdragon 8 Gen 3 | N/A | N/A | ABL Cmdline Injection | Tested | |
| shennong | Xiaomi 14 Pro | Snapdragon 8 Gen 3 | N/A | N/A | ABL Cmdline Injection | Untested | |
| aurora | Xiaomi 14 Ultra | Snapdragon 8 Gen 3 | N/A | N/A | ABL Cmdline Injection | Untested | |
| dada | Xiaomi 15 | Snapdragon 8 Elite | N/A | N/A | ABL Cmdline Injection | Untested | |
| haotian | Xiaomi 15 Pro | Snapdragon 8 Elite | N/A | N/A | ABL Cmdline Injection | Untested | |
| xuanyuan | Xiaomi 15 Ultra | Snapdragon 8 Elite | N/A | N/A | ABL Cmdline Injection | Untested | |
| pudding | Xiaomi 17 | Snapdragon 8 Elite Gen 5 | N/A | N/A | ABL Cmdline Injection | Untested | |
| pandora | Xiaomi 17 Pro | Snapdragon 8 Elite Gen 5 | N/A | N/A | ABL Cmdline Injection | Untested | |
| popsicle | Xiaomi 17 Pro Max | Snapdragon 8 Elite Gen 5 | N/A | N/A | ABL Cmdline Injection | Untested | |
| nezha | Xiaomi 17 Ultra | Snapdragon 8 Elite Gen 5 | N/A | N/A | ABL Cmdline Injection | Untested | |
| liuqin | Xiaomi Pad 6 Pro | Snapdragon 8+ Gen 1 | N/A | N/A | CVE-2025-21479 | Untested | |
| yudi | Xiaomi Pad 6 Max 14 | Snapdragon 8+ Gen 1 | N/A | N/A | CVE-2025-21479 | Untested | |
| sheng | Xiaomi Pad 6S Pro 12.4 | Snapdragon 8 Gen 2 | N/A | N/A | ABL Cmdline Injection | Untested | |
| uke | Xiaomi Pad 7 / POCO Pad X1 | Snapdragon 7+ Gen 3 | N/A | N/A | CVE-2025-21479 | Untested | |
| muyu | Xiaomi Pad 7 Pro | Snapdragon 8s Gen 3 | N/A | N/A | ABL Cmdline Injection | Untested | |
| yupei | Xiaomi Pad 8 | Snapdragon 8s Gen 4 | N/A | N/A | ABL Cmdline Injection | Untested | |
| piano | Xiaomi Pad 8 Pro | Snapdragon 8 Elite | N/A | N/A | ABL Cmdline Injection | Untested | |
| ruyi | Xiaomi MIX Flip | Snapdragon 8 Gen 3 | N/A | N/A | ABL Cmdline Injection | Untested | |
| bixi | Xiaomi MIX Flip 2 | Snapdragon 8 Elite | N/A | N/A | ABL Cmdline Injection | Untested | |
| babylon | Xiaomi MIX Fold 3 | Snapdragon 8 Gen 2 | N/A | N/A | ABL Cmdline Injection | Untested | |
| goku | Xiaomi MIX Fold 4 | Snapdragon 8 Gen 3 | N/A | N/A | ABL Cmdline Injection | Untested | |
| marble | Redmi Note 12 Turbo / POCO F5 | Snapdragon 7+ Gen 2 | Android 15 | 2026-05-01 | CVE-2025-21479 | Tested | |
| sapphiren | Redmi Note 13 NFC | Snapdragon 685 | Android 15 | 2026-01-01 | ABL Cmdline Injection | Tested | |
| creek | Redmi 15 / POCO M7 Pro | Snapdragon 685 | Android 15 | 2026-01-01 | ABL Cmdline Injection | Tested | |
| kunzite | Redmi Note 15 5G | Snapdragon 6 Gen 3 | Android 15 | 2026-02-01 | ABL Cmdline Injection | Tested | Black screen / no display |
| ingres | Redmi K50 Gaming / POCO F4 GT | Snapdragon 8 Gen 1 | Android 14 | 2025-04-01 | CVE-2025-21479 | Tested, unsuccessful | |
| diting | Redmi K50 Ultra / Xiaomi 12T Pro | Snapdragon 8+ Gen 1 | Android 15 | 2025-05-01 | CVE-2025-21479 | Tested | |
| mondrian | Redmi K60 / POCO F5 Pro | Snapdragon 8+ Gen 1 | Android 15 | 2026-02-01 | CVE-2025-21479 | Tested | |
| socrates | Redmi K60 Pro | Snapdragon 8 Gen 2 | N/A | N/A | ABL Cmdline Injection | Untested | |
| vermeer | Redmi K70 / POCO F6 Pro | Snapdragon 8 Gen 2 | N/A | N/A | ABL Cmdline Injection | Untested | |
| manet | Redmi K70 Pro | Snapdragon 8 Gen 3 | N/A | N/A | ABL Cmdline Injection | Untested | |
| onyx | Redmi Turbo 4 Pro / POCO F7 | Snapdragon 8s Gen 4 | N/A | N/A | ABL Cmdline Injection | Tested | Black screen / no display |
| zorn | Redmi K80 / POCO F7 Pro | Snapdragon 8 Gen 3 | N/A | N/A | ABL Cmdline Injection | Untested | |
| miro | Redmi K80 Pro / POCO F7 Ultra | Snapdragon 8 Elite | N/A | N/A | ABL Cmdline Injection | Untested | |
| annibale | Redmi K90 / POCO F8 Pro | Snapdragon 8 Elite | N/A | N/A | ABL Cmdline Injection | Untested | |
| myron | Redmi K90 Pro Max / POCO F8 Ultra | Snapdragon 8 Elite Gen 5 | N/A | N/A | ABL Cmdline Injection | Untested | |

### 3.2 `fenrir` Supported and Known-Affected Devices

Sources:

- Support list: https://github.com/R0rt1z2/fenrir#status
- Official releases: https://github.com/R0rt1z2/fenrir/releases

> Notes:
>
> - "Upstream supported" means the device appears in the `fenrir` README support list; it does not mean that every regional build, OTA, or firmware version has been independently tested.
> - "Official release available" means a prebuilt asset appears on the Releases page; device/firmware matching is still required.
> - Vivo X80 Pro is only listed by the upstream author as known affected, not in the formal support list.

| Codename | Device | Upstream status | Notes |
|---|---|---|---|
| `Pacman` | Nothing Phone (2a) | Upstream supported | NothingOS 3 / 4 official releases available |
| `PacmanPro` | Nothing Phone (2a) Plus | Upstream supported | NothingOS 4 official release; stable Preloader was patched, and the public release has device/firmware-specific prerequisites |
| `Tetris` | CMF Phone 1 | Upstream supported | No official release observed in the source snapshot |
| `peridotl` | Lenovo IdeaTab Pro / Xiaoxin Pad Pro 12.7 | Upstream supported | No official release observed |
| `LG7n` | Tecno Pova 4 | Upstream supported | No official release observed |
| `LG8n` | Tecno Pova 4 Pro | Upstream supported | No official release observed |
| `LH7n` | Tecno Pova 5 | Upstream supported | No official release observed |
| `Q25` | Zinwa Q25 | Upstream supported | No official release observed |
| `duchamp` | Redmi K70E / POCO X6 Pro 5G | Upstream supported | No official release observed |
| `rodin` | Redmi Turbo 4 / POCO X7 Pro | Upstream supported | No official release observed |
| `dash` | Redmi Turbo 5 Max / POCO X8 Pro Max | Upstream supported | No official release observed |
| `xaga` | Redmi Note 11T Pro / Pro+ / POCO X4 GT / Redmi K50i | Upstream supported | No official release observed |
| N/A | Vivo X80 Pro | Known affected; not formally supported | Upstream author reported `bl2_ext` was not verified; no formal port/release observed |

### 3.3 OPPO / Realme / OnePlus

- To be added.

### 3.4 Devices Pending Verification

The following devices were mentioned by public projects/reports but had not been independently verified in the source document (see Section 1.3 for source context):

- Xiaomi 17 series
- Redmi K90 Pro Max
- OnePlus 15 / Ace 6T
- RedMagic 11 series
- Nubia Z80 Ultra

---

# Appendix A — Traversal Summary of Every Unique External Link

**Traversal date:** 2026-08-14 (Asia/Taipei)

The source Markdown contains 148 URL occurrences representing **60 unique URLs**. Duplicate appearances were de-duplicated for this appendix. "Verified" means the target or an authoritative indexed representation was retrievable during this review. "Partially verified" means the exact page was blocked/unavailable to the crawler, so the summary is limited to information visible in the source document or search index.

## A.1 Vulnerability / Exploit / Boot-Chain Research Links

1. **https://github.com/zhuowei/cheese** — **Verified (GitHub).** The current README has evolved into a temporary-root exploit for Quest 3 / 3S based on CVE-2025-21479, while also retaining vulnerability-test material. It explicitly warns that the result is temporary, device/build-specific, security-weakening, and carries brick risk. This is stronger and more platform-specific than treating it as a generic Adreno root tool.

2. **https://github.com/sarabpal-dev/cheese-cake** — **Verified (GitHub).** A work-in-progress adaptation derived from `cheese`, with Samsung-oriented experimentation. The repository itself labels the work WIP; it should not be treated as a universal or stable exploit.

3. **https://www.coolapk.com/feed/70655251?...** — **Partially verified.** Direct retrieval was not available to the crawler. The source README and indexed search result describe a Redmi Note 12 Turbo community test using `cheese` and reporting temporary-root-related behavior. Treat this as a single community signal, not independent confirmation.

4. **https://git.codelinaro.org/.../fb8e864254cdc370...** — **Verified.** Qualcomm/CodeLinaro patch titled **"Fix propagation of untrusted input into kernel cmdline"**. The change constrains the affected OEM input to expected values and supports the report's characterization of the cmdline-injection root cause.

5. **https://git.codelinaro.org/.../78297e8cfe091fc...** — **Verified.** Similar ABL input-validation fix for the `set-hw-fence-value` path, restricting accepted values and preventing extra input from propagating into the kernel command line.

6. **https://github.com/vvb2060/Magica** — **Verified (GitHub).** A privilege-escalation PoC for Android when seccomp is disabled; its README discusses Android 10–16 behavior and the relationship between SELinux permissive state and setuid/seccomp restrictions. It is a conditional post-permissive escalation research path, not a generic root exploit for every Android device.

7. **https://t.me/vvb2060_Channel/17** — **Partially verified.** Direct Telegram content was not retrievable in this environment. The source report uses it as community discussion supporting ABL/SELinux-permissive chaining. Treat the specific claims as community-source material unless independently archived.

8. **https://t.me/vvb2060_Channel/19** — **Partially verified.** Same limitation as the previous Telegram link; retained as a community discussion source.

9. **https://github.com/superturtlee/gbl_root_canoe** — **Verified (GitHub).** EDK2/GBL research for newer Snapdragon platforms. The README explicitly lists demanding prerequisites, including specific Snapdragon generations and a vulnerable ABL; it should not be generalized to all Qualcomm devices. The project also discusses fake-lock/lockmode behavior.

10. **https://github.com/Shocked-Cat/oppo-mtk-fastboot-unlock** — **Verified (GitHub).** Research/tooling that modifies an OPPO-family MediaTek factory Preloader to expose fastboot/unlock behavior. The README has strong model/version limitations and brick warnings; it does not universally bypass later LK/secure-boot verification.

11. **https://github.com/R0rt1z2/fenrir** — **Verified (GitHub).** MediaTek secure-boot-chain PoC exploiting insufficient `bl2_ext` verification under a specific state. It obtains EL3 execution, can affect later verification, and includes lock-state spoofing. Current upstream support is broader than the source snapshot.

12. **https://nvd.nist.gov/vuln/detail/CVE-2022-0847** — **Verified (NVD).** Dirty Pipe: improper pipe-buffer state can allow an unprivileged local user to overwrite page-cache content associated with read-only files and escalate privileges under affected kernels.

13. **https://dirtypipe.cm4all.com/** — **Verified.** Original Dirty Pipe technical write-up explaining the page-cache/pipe-buffer flaw, affected kernel history, and exploitation concept.

14. **https://github.com/polygraphene/DirtyPipe-Android** — **Verified (GitHub).** Android temporary-root PoC with narrowly defined targets, originally Pixel 6 builds from early 2022. The README explicitly warns against use on unmatched devices/builds and notes that reboot removes the temporary root state.

15. **https://github.com/tiann/DirtyPipeRoot** — **Verified (GitHub).** Another Android temporary-root project based on Dirty Pipe. It emphasizes temporary root only, brick risk, and loss of root after reboot.

16. **https://nvd.nist.gov/vuln/detail/CVE-2026-43499** — **Verified (NVD).** GhostLock root cause: `remove_waiter()` can operate on `current` rather than `waiter::task` during proxy-lock rollback, leaving an uncleared `pi_blocked_on` pointer and creating a UAF condition.

17. **https://github.com/NebuSec/CyberMeowfia/tree/main/IonStack/CVE-2026-43499/exploit** — **Verified (GitHub).** The exact subtree contains a Makefile, source directory, and exploit assets; this confirms that the public material includes exploit implementation, not only a write-up.

18. **https://github.com/x-spy/CVE-2026-43499-popsicle** — **Verified (GitHub).** Android 16/Xiaomi 17-family adaptation for a specific kernel build. It generates target data from firmware artifacts and documents post-exploit framework instability that clears after reboot. Strongly firmware-specific.

19. **https://github.com/Linuxoid-cn/CVE-2026-43499-Poc-Analysis** — **Verified (GitHub).** General Android arm64 adaptation framework that generates `target.h` from a target `boot.img` plus a device profile and builds a matching payload. The README explicitly says actual exploitability depends on successful target generation and real-device testing.

20. **https://github.com/BuSung-dev/Root-My-Galaxy** — **Verified (GitHub).** One-click installer/front end for explicitly supported Samsung model/kernel combinations. Payloads and offsets are separate and selected by model/kernel-version matching; this reinforces that the approach is not universal.

21. **https://github.com/J-jaeyoung/bad-epoll** — **Verified (GitHub).** Authoritative Bad Epoll write-up/PoC. It presents a complete Linux kernelCTF exploit, affected-version history, and an Android status section. The README still states that the full Android root exploit is in progress, while the Pixel 10 UAF trigger is demonstrated.

22. **https://nvd.nist.gov/vuln/detail/CVE-2026-46242** — **Verified (NVD via search index).** NVD describes an `eventpoll` UAF involving `ep_remove()` / file lifetime and gives a local, low-privilege, no-user-interaction CVSS vector. The root cause aligns with the Bad Epoll project.

23. **https://www.openwall.com/lists/oss-security/2026/07/08/13** — **Verified.** oss-security disclosure for Bad Epoll, including the Linux kernel UAF context and Android relevance.

24. **https://source.android.com/docs/security/bulletin/2026/2026-03-01** — **Verified (official Android).** March 2026 Android bulletin. It explicitly notes indications of **limited, targeted exploitation** for CVE-2026-21385 and identifies the relevant patch level.

25. **https://docs.qualcomm.com/product/publicresources/securitybulletin/march-2026-bulletin.html** — **Verified (Qualcomm page reachable).** Qualcomm's March 2026 security bulletin is the vendor advisory for CVE-2026-21385 and its affected chipset scope.

26. **https://nvd.nist.gov/vuln/detail/CVE-2026-21385** — **Verified (NVD via search index).** NVD describes memory corruption related to allocation alignment, shows Qualcomm's CVSS 7.8 (High), and records the CVE in CISA's KEV catalog.

27. **https://github.com/vocolboy/RemoteKeyAttestation** — **Verified (GitHub).** RKA/RKP implementation notes and demonstration. The README shows a test flow involving an unlocked device, BASIC integrity, custom TrickyStore/RKA handling, and a STRONG_INTEGRITY result. It should be treated as a research implementation and trust-boundary demonstration—not proof that arbitrary network MITM can forge hardware attestation.

## A.2 Forensics / Android Platform / Verification Documentation

28. **https://csrc.nist.gov/pubs/sp/800/101/r1/final** — **Verified (NIST).** NIST SP 800-101 Rev. 1, guidelines for mobile-device forensics. It provides a methodological framework for acquisition, examination, analysis, and reporting rather than an exploit.

29. **https://developer.android.com/tools/adb** — **Verified (official Android).** Official ADB documentation: device communication, shell, application installation, debugging, and related development operations.

30. **https://developer.android.com/training/data-storage/app-specific** — **Verified (official Android).** Official documentation for App-specific storage and isolation. Supports the report's point that ordinary ADB/other Apps do not automatically gain access to another App's private internal data.

31. **https://www.magnetforensics.com/resources/magnet-acquire/** — **Verified.** Product/resource page for Magnet Acquire, a digital-forensics acquisition tool. Relevant as an example of lawful forensic acquisition tooling.

32. **https://belkasoft.com/x** — **Verified.** Belkasoft X forensic platform page. Relevant to multi-source digital evidence acquisition/analysis; it is not evidence that ordinary ADB alone bypasses Android sandboxing.

33. **https://www.oxygenforensics.com/technical-resources/android-agent/** — **Verified.** Oxygen Forensics material describing Android agent-assisted acquisition. Relevant to the report's taxonomy of agent-based logical acquisition.

## A.3 GBL / AVB / Bootloader Documentation and Research

34. **https://www.androidauthority.com/qualcomm-snapdragon-8-elite-gbl-exploit-bootloader-unlock-3648651/** — **Verified.** Third-party news/analysis covering the Qualcomm GBL bootloader-unlock research and reported newer Snapdragon/Xiaomi implications. Useful for context, but less authoritative than AOSP and project sources.

35. **https://source.android.com/docs/core/architecture/bootloader/generic-bootloader** — **Verified (AOSP).** Official Generic Bootloader (GBL) overview. Describes GBL as a standardized/updatable bootloader component and documents its role, UEFI interfaces, and Android integration.

36. **https://android.googlesource.com/platform/bootable/libbootloader/+/refs/heads/main/gbl/docs/gbl_fastboot.md** — **Verified (AOSP source).** GBL fastboot design documentation covering transport and bootloader-side fastboot behavior. Useful for understanding what GBL is expected to implement.

37. **https://source.android.com/docs/core/architecture/bootloader/locking_unlocking** — **Verified (AOSP).** Official Android bootloader locking/unlocking model and security semantics.

38. **https://github.com/atlas4381/qualcomm_avb_exploit_poc** — **Verified (GitHub).** PoC for Qualcomm ABL/Keymaster device-state manipulation. The README explicitly requires prior write access to partition tables/partitions and explains `NO_AVB` / milestone behavior; it is not a first-stage root exploit.

39. **https://github.com/kasnria001/qualcomm_noavb_exploit_common** — **Verified (GitHub).** Work-in-progress research on a `NO_AVB` bootloader-unlock path and newer-device obstacles such as pvmfw and changes from QSEECom to SMCInvoke. It is exploratory and not a universal ready-made solution.

40. **https://github.com/Linuxoid-cn/Mi8G3-Unlocker** — **Verified (GitHub).** Windows automation for selected Xiaomi Snapdragon 8 Gen 3 devices on newer HyperOS. README lists model-specific resources and warns about data loss/device damage.

41. **https://github.com/Linuxoid-cn/Mi8G2-Unlocker** — **Verified (GitHub).** Windows automation for selected Snapdragon 8 Gen 2 Xiaomi devices. The README states HyperOS 2+ and a security-patch ceiling of 2026-02-01 for the relevant path.

42. **https://github.com/Linuxoid-cn/Mi8sG4-Unlocker** — **Verified (GitHub).** Model-specific Snapdragon 8s Gen 4 Xiaomi unlock automation with matching ABL/GPT/Ennea resources. It remains highly device-specific.

43. **https://github.com/Linuxoid-cn/Mi8sG3orMi7pG3-Unlocker** — **Verified (GitHub).** Automation for selected Snapdragon 8s Gen 3 / 7+ Gen 3 Xiaomi devices. The current README claims its workflow supports all patch versions; this is a project claim, not an OEM guarantee.

44. **https://github.com/Linuxoid-cn/Mi8E-Unlocker** — **Verified (GitHub).** Automation for selected Snapdragon 8 Elite Xiaomi devices. The README states HyperOS 2+ and a security-patch ceiling of 2026-02-01 for its relevant path.

45. **https://git.codelinaro.org/.../1b2e5f9c4e95db4c...** — **Verified indirectly through the PoC and source references.** This is the Qualcomm ABL fix that changes AVB2 status determination toward a compile-time setting, preventing runtime GPT manipulation from steering the bootloader into the vulnerable `NO_AVB` state.

46. **https://mitanyan98.hatenablog.com/entry/2026/06/13/095658** — **Verified.** Third-party technical breakdown/risk record of the Qualcomm/Xiaomi unlock workflow. Useful as independent process commentary, but not an OEM or Qualcomm primary source.

## A.4 MediaTek LK / Certificate / Device-State References

47. **https://github.com/R0rt1z2/lkpatcher** — **Verified (GitHub).** Python LK modification/patching tool. Current README exposes patch categories and `--cert-bypass` modes, while warning that `seccfg`, SBC, and device-specific support still matter.

48. **https://github.com/kasnria001/pwnage24mtk** — **Verified (GitHub).** Research implementation for a MediaTek ASN.1 certificate-verification logic flaw, described by its author as similar to CVE-2023-20696. It distinguishes older and newer parser behavior and is explicitly research-only.

49. **https://github.com/R0rt1z2/lkpatcher/blob/master/lkpatcher/cert_bypass.py** — **Verified (GitHub file).** Implements two strategies: `WRAP`, which wraps the original signed certificate and appends modified hashes, and `OVERRIDE`, which prepends a hash-override block before the untouched certificate. This confirms the two modes described in the source report.

50. **https://github.com/R0rt1z2/lkpatcher/blob/master/patches.json** — **Verified (GitHub file).** Default binary-patch categories visible in the file include `fastboot`, `dm_verity`, `orange_state`, and `red_state`.

51. **https://nvd.nist.gov/vuln/detail/CVE-2023-20696** — **Verified (NVD).** MediaTek vulnerability record affecting a listed set of chips/platforms. NVD/MediaTek classify it as a local, high-privilege issue with memory-safety impact. It supports the "similar to" context but does not establish that every newer cert-bypass variant is the same CVE.

52. **https://source.android.com/docs/security/features/verifiedboot/device-state** — **Verified (AOSP).** Official Verified Boot device-state documentation, including the conceptual `LOCKED` / `UNLOCKED` model.

53. **https://www.mi.com/tw/support/faq/details/KA-21069/** — **Verified.** Xiaomi Taiwan bootloader-unlock support/FAQ page. Useful for contrasting official account/device authorization with unofficial boot-chain modification.

54. **https://trust.mi.com/docs/miui-security-white-paper-global/3/1** — **Verified.** Xiaomi security white paper section on secure storage. Provides OEM security-model context for protected device state.

55. **https://4pda.to/forum/index.php?showtopic=721838&st=59800** — **Verified via search index.** Community discussion describes Xiaomi MediaTek unlock behavior involving both `seccfg` and RPMB, and explains that stock LK may require both conditions while patched LK can alter the check. This is community reverse engineering, not an OEM specification.

## A.5 Post-root Biometric / AuthToken References

56. **https://www.darknavy.org/blog/the_biometric_authtoken_heist/#disclosure** — **Verified.** Disclosure section of DARKNAVY's research. It states that the reported issues were responsibly disclosed and largely fixed/confirmed by vendors.

57. **https://www.darknavy.org/blog/the_biometric_authtoken_heist/** — **Verified.** Full research article. DARKNAVY reports testing 30+ devices from 9 manufacturers and recovering PINs on 8 devices from 7 vendors; six cases worked in BFU and enabled CE decryption, two were AFU-only. The threat model explicitly assumes Android-side root/kernel control first.

58. **https://security.samsungmobile.com/securityUpdate.smsb?month=06&year=2025** — **Verified (Samsung official).** Samsung's June 2025 SMR maps:
   - CVE-2025-20987: improper access control in fingerprint trustlet; local privileged attacker can obtain an `auth_token`.
   - CVE-2025-20988: out-of-bounds read in fingerprint trustlet.
   - CVE-2025-20989: improper logging in fingerprint trustlet; local privileged attacker can obtain an HMAC key.
   This official mapping differs from the ordering used in one portion of DARKNAVY's disclosure, so the source report correctly chose Samsung's mapping for the final table/text.

## A.6 `fenrir` Status / Release Links

59. **https://github.com/R0rt1z2/fenrir#status** — **Verified through the current README.** The current upstream list now includes **Xiaomi 12T (`plato`)** in addition to the devices represented in the source snapshot. Therefore, the translated Section 3.2 preserves the original table, while this appendix records the later upstream change.

60. **https://github.com/R0rt1z2/fenrir/releases** — **Verified (GitHub releases).** Prebuilt releases are concentrated on Nothing `Pacman` / `PacmanPro`. Current NothingOS 4 release notes explicitly warn that stable firmware has patched the original condition and requires device/firmware-specific prerequisites, including an unlocked bootloader for those release procedures.

---

# Appendix B — Important Updates / Discrepancies Found During Traversal

1. **`fenrir` support list changed after the source snapshot.**  
   The current README includes **Xiaomi 12T (`plato`)**, so the source statement that upstream lists 12 supported codenames is now stale. The original translation is preserved; Appendix A records the newer state.

2. **`cheese` is now more than a minimal vulnerability test.**  
   Its current README presents a Quest 3 / 3S temporary-root exploit for specific affected builds using CVE-2025-21479. This should still not be generalized to unrelated Adreno devices.

3. **GhostLock Android work is real but highly firmware-specific.**  
   The exact CyberMeowfia exploit subtree contains source/build material, while the `popsicle` port is tied to a specific Xiaomi 17-family Android 16 kernel build. The generic Android adaptation project also requires target-specific `boot.img` and profile data.

4. **Bad Epoll's Linux exploit is complete, but the current project still distinguishes Android status.**  
   The public Linux/kernelCTF chain is complete; Pixel 10 can trigger the UAF, while the project's README says the complete Android root exploit is still in progress.

5. **CVE-2026-21385 is a confirmed exploited-priority issue, but not a public universal root exploit.**  
   Android reports limited targeted exploitation; NVD records CISA KEV inclusion and Qualcomm CVSS 7.8. Device impact should be evaluated by actual chipset/firmware patch status.

6. **Samsung vs. DARKNAVY CVE-type ordering differs.**  
   Samsung's official June 2025 advisory is the safer authority for CVE-to-vulnerability-type mapping. The translated report preserves the source author's decision to follow Samsung.

7. **Community-only evidence remains community-only.**  
   The Coolapk, Telegram, and 4PDA material is useful for leads and feasibility signals but should not be given the same evidentiary weight as Qualcomm/AOSP/NVD/vendor advisories or directly inspected GitHub source.
