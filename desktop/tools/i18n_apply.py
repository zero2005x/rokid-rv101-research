#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
i18n_apply.py — merge translation fragments into tools/i18n_master.json and fill the gaps.

Usage:
  python3 tools/i18n_apply.py                 # fill gaps only (untranslated entries use English)
  python3 tools/i18n_apply.py frag1.json ...  # merge fragments, then fill gaps
  python3 tools/i18n_apply.py --status        # show per-locale translation progress

Fragment format (only the locales and keys to update; the rest may be omitted):
  {
    "de": { "BTN_APPLY": "Übernehmen", "BTN_CANCEL": "Abbrechen" },
    "fr": { "BTN_APPLY": "Appliquer" }
  }

Filling rule: if a (key, locale) pair is empty or absent, that key's en value is used, so
i18n_master.json is always "complete", the generator can run at any time and the program can
always compile.
"""

import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MASTER = os.path.join(ROOT, "tools", "i18n_master.json")


def load():
    with open(MASTER, encoding="utf-8") as f:
        return json.load(f)


def save(data):
    with open(MASTER, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write("\n")


def status(data):
    codes = [l["code"] for l in data["locales"]]
    strings = data["strings"]
    total = len(strings)
    print("翻譯進度（與英文相同者視為未翻譯）：")
    print("  %-8s %6s %6s %8s" % ("locale", "已譯", "待譯", "完成度"))
    done_all = True
    for lc in codes:
        if lc == "en":
            continue
        same = 0
        for key, e in strings.items():
            v = e.get(lc, "")
            if not v or v == e.get("en", ""):
                same += 1
        pct = 100.0 * (total - same) / total if total else 0.0
        if same:
            done_all = False
        print("  %-8s %6d %6d %7.1f%%" % (lc, total - same, same, pct))
    print()
    if done_all:
        print("所有語系皆已翻譯完成。")
    else:
        print("仍有語系使用英文暫代 —— 需要套用翻譯片段。")
    return 0 if done_all else 1


def main():
    argv = sys.argv[1:]
    if "--status" in argv:
        return status(load())

    data = load()
    codes = [l["code"] for l in data["locales"]]
    strings = data["strings"]

    applied = 0
    for path in argv:
        with open(path, encoding="utf-8") as f:
            frag = json.load(f)
        for lc, kv in frag.items():
            if lc not in codes:
                print("警告：片段含未知 locale %r，略過" % lc, file=sys.stderr)
                continue
            if not isinstance(kv, dict):
                print("警告：%s 的值不是 {key: text}，略過" % lc, file=sys.stderr)
                continue
            for key, val in kv.items():
                if key not in strings:
                    print("警告：未知 key %r（locale %s），略過" % (key, lc), file=sys.stderr)
                    continue
                if not isinstance(val, str):
                    print("警告：%s.%s 不是字串，略過" % (lc, key), file=sys.stderr)
                    continue
                strings[key][lc] = val
                applied += 1
        print("已併入 %s" % path)

    # ---- fill in the gaps ----
    # NOTE: en is the "baseline language" and must never be backfilled from another locale —
    # otherwise Chinese gets written into en and pollutes every locale temporarily using English
    # (this really happened).
    missing_en = [k for k, e in strings.items() if not e.get("en")]
    if missing_en:
        print("錯誤：以下 key 缺少 en 基準值，請先補上再執行：", file=sys.stderr)
        for k in missing_en[:20]:
            print("  - " + k, file=sys.stderr)
        return 1

    filled = 0
    for key, e in strings.items():
        ref = e["en"]
        for lc in codes:
            if not e.get(lc):
                e[lc] = ref
                filled += 1

    # reorder each key's fields to follow the locales order, keeping diffs stable
    for key, e in strings.items():
        strings[key] = {lc: e.get(lc, "") for lc in codes}

    save(data)
    print("已套用 %d 筆翻譯，補齊 %d 筆缺漏 → %s" % (applied, filled, MASTER))
    return 0


if __name__ == "__main__":
    sys.exit(main())
