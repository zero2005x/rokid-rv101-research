#!/usr/bin/env python3
"""Batch disassemble RV101 kernel functions to files."""
import sys
sys.path.insert(0, "scripts/ghostlock_analysis")
from disasm_func import disasm

K = "binaries/extracted_1.22/kernel"
OUT = "research/ghostlock_refs/rv101_disasm"

FUNCS = {
    "rt_mutex_adjust_prio_chain": (0xffffffc0081ceaf4, 0xffffffc0081cf3bc),
    "task_blocks_on_rt_mutex":    (0xffffffc0081cfbac, 0xffffffc0081cff08),
    "rt_mutex_start_proxy_lock":  (0xffffffc0081cff08, 0xffffffc0081cffb0),
    "futex_wait_requeue_pi":      (0xffffffc0082366b4, 0xffffffc008237f28),
    "futex_requeue":              (0xffffffc008234360, 0xffffffc00823526c),
    "futex_lock_pi":              (0xffffffc008235a70, 0xffffffc0082360c8),
    "commit_creds":               (0xffffffc00818b328, 0xffffffc00818ba9c),
    "prepare_kernel_cred":        (0xffffffc00818ba9c, 0xffffffc00818c3a0),
}

for name, (start, end) in FUNCS.items():
    disasm(K, start, end, f"{OUT}/{name}.txt")
    print("---")
