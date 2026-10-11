#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_exe.py — 驗證編譯後的 .exe 真的含有全部語系字串。

為什麼需要這個：gen_i18n.py 只驗證「來源資料」正確，無法證明
  (1) 產生的表頭有被編進執行檔，
  (2) 編譯器把 L"..." 正確編成 UTF-16LE，
  (3) 沒有任何字串在編譯期被截斷或合併掉。
本腳本直接從 PE 的唯讀資料段取出位元組，逐字串比對 UTF-16LE 編碼。

用法：
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
    回傳 PE 中含初始化資料的區段名。
    注意：objcopy -h 印的是說明，區段表要用 objdump -h。
    objdump -h 每行格式：
      Idx Name  Size  VMA  LMA  File off  Algn   （下一行才是 Flags）
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
        # 字串只會落在 .rdata/.data/.text；.debug_* 可能被 objcopy 補齊到
        # 檔案位移而膨脹成 GB 級，且不可能含字串常值。
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

    # 把各區段抓成單一 blob
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

    # 額外檢查：阿拉伯文確實存在且不含 bidi 控制字元
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
