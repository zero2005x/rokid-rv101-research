# Redaction Checklist

> English: [Redaction_Checklist_EN.md](Redaction_Checklist_EN.md)

Two copies of every sensitive artifact must be maintained:

1. **Immutable/raw** copy → `confidential/raw_evidence/` (never edited in place).
2. **Redacted disclosure** copy → `13_Redacted_Public_Materials/` (safe for broader /
   eventual-public use).

## Must be redacted / kept confidential (until coordinated disclosure)

- [ ] Kernel virtual addresses (KASLR base/slide, `printk_formats` anchors, `init_cred`,
      `selinux_state`, task/cred/fops addresses, leaked `mm` addresses).
- [ ] Exploit internals: `target_rv101.h` offsets, forged fake-page layout, SKB/reclaim
      parameters, pselect/rb_erase "punch" primitive, FOPS install mechanics.
- [ ] Exploit source code and compiled binary (`ghostlock_rv101`).
- [ ] Exact reproduction commands that reveal the above.
- [ ] Researcher host paths, Windows username, and local toolchain paths
      (e.g., `C:\Users\<USER>\...`) that appear in the operation notes.
- [x] **Device serial — 決策：withhold（2026-09-26）**。`13_Redacted_Public_Materials/post_exploit_state_REDACTED.txt`
      原本在檔頭與 `Device identifier:` 兩處仍含完整序號，**已改為 `<REDACTED-DEVICE-ID>`**，
      `SHA256SUMS.txt` 對應條目已同步更新（備份存於 `confidential/…bak-20260926`）。
      ⚠ **`05_Post_Exploit_State/post_exploit_state_REDACTED.txt` 是同內容的另一份，尚未同步處理**（非公開發佈區）。
- [ ] Any third-party keys/secrets that may appear in logs (e.g., OTA keys, ADB keys).
- [ ] The full partition dumps and `ls -l /dev/block/by-name` output (reveals device
      layout; keep in raw/confidential only).

## Safe to include in redacted / public material

- [ ] `id` / `id -Z` / `getenforce` / `Uid/Gid/Cap*` lines (the core proof).
- [ ] Firmware/build fingerprint, Android version, kernel version string, security patch.
- [ ] The statement that system/vendor remained read-only.
- [ ] The non-destructive demonstration (creation of `/data/root_proof`).
- [ ] CVE-2026-43499 references and the affected/unpatched relationship.

## Procedure

1. Keep raw artifacts untouched; hash them (`12_Hashes/SHA256SUMS.txt`).
2. Produce a redacted copy; verify no addresses/offsets remain with a grep for
   `0xffff`, `slide=`, `init_cred`, `target_rv101`, and `C:\Users`.
3. Hash the redacted copy separately and record raw→redacted pairing in
   `Evidence_Index.md`.
4. Route the redacted copy, not the raw, for any non-vendor or public use.

## Current redacted artifact

- `05_Post_Exploit_State/post_exploit_state_REDACTED.txt` (derived from
  `run_w2_v960_143215.log`). Addresses and exploit internals removed; before/after state
  preserved verbatim.
