# Glasses Media Index Repair — A/B Experiment Samples (2026-09-13)

> 中文版：[README.md](README.md)

## Related report
`reports/wifi_transfer_fix_2026-09-13.md` Rev.3 §13–§16, Rev.5 §17–§18 (FACTS.md §7 P29, P30).

## Background
The glasses-side `media/index.json` is a **single full JSON overwrite**, and it has two fragile points:

1. The parse loop in `load()` **shares a single try/catch** — if any one `MediaItem.fromJson` throws,
   the outer catch swallows it and **none of the items after it are loaded**; then any subsequent `persist()`
   (triggered by taking a photo / recording video / generating a thumbnail)
   **permanently writes back** this shortened list.
2. `persist()` uses `File.writeText()` (truncate first, then write), so if the process is killed midway it leaves an empty or half-written file.

## Files

| File | Contents |
|------|------|
| `index_repaired_9items.json` | The index after the fixed App self-repaired it (9 entries, disk = index consistent) |
| `index_before_delete_9items.json` | Index snapshot **before** deleting 3 unfinished videos (9 entries, including the three of 636 MB / 664 MB / 111 MB) |
| `index_ab_corrupt_entry4.json` | **For the A/B experiment**: the required field `fileName` removed from the 4th entry in the table above, the other 8 entries intact |
| `ab_victim_file.txt` | Filename of the corrupted entry (`VID_1789152641290.mp4`) |
| `deleted_media_ids.txt` | The 3 mediaIds deleted at the user's instruction |

## A/B result (same corrupted index, same device)

| APK | log | List seen on the phone |
|-----|-----|---------------|
| Old `1.0-safe` (code 3) | `E MediaRepository: index load failed` + the stack swallowed by the outer catch | **total=4** ← after the 4th entry **all 5 remaining entries vanished** |
| New `1.0-safe.2` (code 4) | `E media entry #4 skipped: No value for fileName`<br>`W index reconciled: 1 file(s) re-registered from disk` | **total=9** ← not a single entry lost |

## Fixes (`android/app-glasses/…/media/MediaRepository.kt`)
1. Parsing changed to a **per-entry try/catch** (a single bad entry only skips itself).
2. Added **`reconcileWithDisk()`**: after loading, scan each album directory and re-register files that are
   "on disk but not in the index" — the index is no longer the only source of truth, and the list can self-repair.
3. `persist()` changed to an **atomic write** (write `index.json.tmp` first, then `renameTo()`).

## On-device validation
```
23:07:20 W MediaRepository: index reconciled: 6 file(s) re-registered from disk
23:18:22 E media entry #4 skipped: No value for fileName
23:18:22 W index reconciled: 1 file(s) re-registered from disk
23:30:02 (restart after deleting the 3) no reconcile message at all ⇒ disk and index agree
```

> ⚠ This directory is also **evidence**, not scratch space: `index_ab_corrupt_entry4.json` is a deliberately corrupted specimen,
> so do not copy it back to the device as a usable index file.
