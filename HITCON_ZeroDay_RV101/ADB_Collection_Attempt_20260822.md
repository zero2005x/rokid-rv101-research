# ADB 即時蒐證嘗試 — 2026-08-22

> English: [ADB_Collection_Attempt_20260822_EN.md](ADB_Collection_Attempt_20260822_EN.md)

> 此檔為內部狀態紀錄，不建議作為 HITCON 證據附件。

## 觀察

- 開始：2026-08-22T17:21:40.461+08:00
- `adb devices -l` 當下列出：

```text
List of devices attached
[DEVICE_SERIAL_REDACTED] device product:glasses model:RG_glasses device:glasses transport_id:53
```

- `adb get-state` 先輸出 `device`，隨即回傳：

```text
error: closed
```

- 後續 shell 指令皆顯示 `device '[DEVICE_SERIAL_REDACTED]' not found`。
- 2026-08-22 約 17:22 執行 `adb start-server` 與 `adb reconnect offline` 後，`adb devices -l` 仍為空。

## 證據判定

- 此次只確認 ADB transport 曾短暫看見指定 serial／model：CONFIRMED。
- 此次未取得新的 `getprop`、kernel 或 privilege baseline：UNKNOWN／未蒐集成功。
- 不應把前次（2026-08-12／2026-08-22）證據混寫成 17:21 新蒐集資料。

## 重新連線後的優先蒐集順序

1. `adb devices -l`
2. Device identity／firmware／security patch／kernel
3. Pre-exploit `id`、`id -Z`、`whoami`、`getenforce`、capabilities
4. 若研究者明確要重跑 PoC，再另開完整、連續、帶 timestamp 的 before→after session；不要在連線不穩時執行 exploit。
