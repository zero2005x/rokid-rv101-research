#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_exe.py — verify that the compiled .exe really contains every locale string.

Why this is needed: gen_i18n.py only validates that the "source data" is correct; it cannot prove
  (1) the generated header was actually compiled into the executable,
  (2) the compiler encoded L"..." as UTF-16LE correctly,
  (3) no string was truncated or merged at compile time.
This script takes the bytes straight out of the PE's read-only data sections and compares each
string against its UTF-16LE encoding.

Usage:
  python3 tools/verify_exe.py zero_light_desktop.exe
  python3 tools/verify_exe.py zero_light_desktop.exe --objcopy x86_64-w64-mingw32-objcopy
"""

import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MASTER = os.path.join(ROOT, "tools", "i18n_master.json")


def sections_with_data(objdump, exe):
    """
    Return the names of the PE sections that hold initialised data.
    NOTE: objcopy -h prints help; the section table comes from objdump -h.
    Each objdump -h line looks like:
      Idx Name  Size  VMA  LMA  File off  Algn   (Flags are on the next line)
    """
    out = subprocess.run([objdump, "-h", exe], capture_output=True, text=True).stdout
    names = []
    for line in out.splitlines():
        m = re.match(r"^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+"
                     r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", line)
        if not m:
            continue
        name = m.group(1)
        size = int(m.group(2), 16)
        # Strings can only land in .rdata/.data/.text; .debug_* can be padded by objcopy out to
        # file offsets and balloon to gigabytes, and cannot contain string literals.
        if size > 0 and name in (".rdata", ".data", ".text"):
            names.append(name)
    return names


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    exe = sys.argv[1]
    objcopy = "x86_64-w64-mingw32-objcopy"
    objdump = "x86_64-w64-mingw32-objdump"
    if "--objcopy" in sys.argv:
        objcopy = sys.argv[sys.argv.index("--objcopy") + 1]
        objdump = objcopy.replace("objcopy", "objdump")

    if not os.path.exists(exe):
        print("找不到執行檔：%s" % exe, file=sys.stderr)
        return 1

    with open(MASTER, encoding="utf-8") as f:
        data = json.load(f)
    codes = [l["code"] for l in data["locales"]]
    strings = data["strings"]

    # pull the sections into a single blob
    blob = bytearray()
    secs = sections_with_data(objdump, exe)
    if not secs:
        print("無法列舉 PE 區段（objcopy 失敗？）", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory() as td:
        for i, s in enumerate(secs):
            binp = os.path.join(td, "sec%d.bin" % i)
            r = subprocess.run([objcopy, "-O", "binary", "--only-section=" + s, exe, binp],
                               capture_output=True)
            if r.returncode == 0 and os.path.exists(binp):
                with open(binp, "rb") as fh:
                    blob += fh.read()

    print("已載入 %d 個區段，共 %d bytes：%s" % (len(secs), len(blob), ", ".join(secs)))

    missing = []
    total = 0
    for key, entry in strings.items():
        for lc in codes:
            v = entry.get(lc)
            if not v:
                continue
            total += 1
            needle = v.encode("utf-16-le")
            if needle not in blob:
                missing.append((key, lc, v))

    if missing:
        print("\n缺少 %d / %d 筆字串：" % (len(missing), total), file=sys.stderr)
        for key, lc, v in missing[:40]:
            print("  - %s [%s] = %r" % (key, lc, v[:60]), file=sys.stderr)
        if len(missing) > 40:
            print("  ...（其餘 %d 筆省略）" % (len(missing) - 40), file=sys.stderr)
        return 1

    print("\n✓ 全部 %d 筆翻譯（%d 語系 × %d 字串）皆已正確編入 %s"
          % (total, len(codes), len(strings), os.path.basename(exe)))

    # extra check: Arabic is present and contains no bidi control characters
    BIDI = set([0x200E, 0x200F, 0x061C] + list(range(0x202A, 0x202F)) + list(range(0x2066, 0x206A)))
    ar_bad = [(k, v) for k, v in ((k, e.get("ar", "")) for k, e in strings.items())
              if any(ord(c) in BIDI for c in v)]
    if ar_bad:
        print("警告：阿拉伯文字串含 bidi 控制字元：%s" % [k for k, _ in ar_bad][:10],
              file=sys.stderr)
        return 1
    print("✓ 阿拉伯文無 bidi 控制字元（RTL 由 WS_EX_LAYOUTRTL 處理）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
