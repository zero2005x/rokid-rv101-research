# Live ADB Evidence-Collection Attempt — 2026-08-22

> 中文版：[ADB_Collection_Attempt_20260822.md](ADB_Collection_Attempt_20260822.md)

> This file is an internal status record and is not recommended as a HITCON evidence attachment.

## Observations

- Start: 2026-08-22T17:21:40.461+08:00
- At that moment `adb devices -l` listed:

```text
List of devices attached
[DEVICE_SERIAL_REDACTED] device product:glasses model:RG_glasses device:glasses transport_id:53
```

- `adb get-state` first printed `device`, then immediately returned:

```text
error: closed
```

- Subsequent shell commands all showed `device '[DEVICE_SERIAL_REDACTED]' not found`.
- After running `adb start-server` and `adb reconnect offline` at about 17:22 on 2026-08-22, `adb devices -l` was still empty.

## Evidence Assessment

- This time only confirmed that the ADB transport briefly saw the specified serial/model: CONFIRMED.
- No new `getprop`, kernel, or privilege baseline was obtained this time: UNKNOWN / collection was not successful.
- The previous evidence (2026-08-12 / 2026-08-22) must not be mixed together and written up as newly collected data from 17:21.

## Priority Collection Order After Reconnecting

1. `adb devices -l`
2. Device identity / firmware / security patch / kernel
3. Pre-exploit `id`, `id -Z`, `whoami`, `getenforce`, capabilities
4. If the researcher explicitly wants to re-run the PoC, then open a separate complete, continuous, timestamped before→after session; do not run the exploit while the connection is unstable.
