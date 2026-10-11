# Crash Logs

Crash/panic evidence relevant to exploit development is kept in the research repo
(reference only; not duplicated here unless needed):

- `reports/ghostlock_runlogs/run_w2_clean_v958_133632.log` — kernel-side gid-repair
  panic (motivates the user-space `setgid(0)` approach used in the final v9.59/v9.60).
- `reports/GHOSTLOCK_V3_PANIC_ROOTCAUSE_ANALYSIS.md` — fake-page panic root-cause.

These are development artifacts, not required for the core finding, but support the
"controlled-write → panic" reliability discussion if the vendor asks.
