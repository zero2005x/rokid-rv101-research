#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_i18n.py — 由 tools/i18n_master.json 產生 src/i18n_strings.h

用途：ZeroLightRokid Desktop 的多國語系字串表產生器 / 驗證器。

  python3 tools/gen_i18n.py            # 驗證 + 產生 src/i18n_strings.h
  python3 tools/gen_i18n.py --check    # 只驗證（CI 用，不寫檔）

驗證項目：
  1. 每個 key 在每個 locale 都有非空字串
  2. 同一個 key 的 printf 格式符（%s/%d/%ls…）在所有 locale 完全一致（順序與數量）
  3. locale 代碼 / win_primary 唯一且合法
  4. 不含 C 字串無法安全表示的控制字元
  5. 每個 locale 都有 name / font

產出：src/i18n_strings.h（UTF-8，供 MinGW 與 MSVC 以 /utf-8 編譯）
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MASTER = os.path.join(ROOT, "tools", "i18n_master.json")
OUT = os.path.join(ROOT, "src", "i18n_strings.h")

# printf 格式符：%[flags][width][.prec][length]conv
# 以「完整比對」方式逐一掃描，而非 finditer —— 這樣才能抓出落單的 '%'。
FMT_RE = re.compile(
    r"%(?:[-+ #0]*)(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:hh|h|ll|l|L|z|j|t|I64|I32)?([diouxXeEfgGaAcspn%])"
)


def scan_fmt(s):
    """
    掃描格式字串，回傳 token 序列；每個 token 是規格化後的格式符，或 '%%'。

    落單的 '%'（既非合法格式符也非 '%%'）會拋出 ValueError —— 這在執行期會導致
    swprintf 讀取不存在的引數，是很危險的錯誤，必須在產生階段就擋下。
    """
    toks = []
    i = 0
    n = len(s)
    while i < n:
        if s[i] != "%":
            i += 1
            continue
        m = FMT_RE.match(s, i)
        if not m:
            raise ValueError("落單的 '%%' 於位置 %d（前後文：%r）" % (i, s[max(0, i - 8):i + 8]))
        if m.group(1) == "%":
            toks.append("%%")
        else:
            # 去掉空白、保留長度修飾詞（%s 與 %ls 必須區分）
            toks.append(m.group(0).replace(" ", ""))
        i = m.end()
    return toks


def die(msg):
    print("ERROR: " + msg, file=sys.stderr)
    sys.exit(1)


def c_escape(s):
    """把字串轉成 C 寬字串字面值的內容（不含外層 L"..."）。"""
    out = []
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif ord(ch) < 0x20 or ord(ch) == 0x7F:
            out.append("\\x%04x" % ord(ch))
        else:
            out.append(ch)
    return "".join(out)


def c_ident(key):
    """key → C 識別字（STR_ 前綴在產生時加）。"""
    ident = re.sub(r"[^0-9A-Za-z_]", "_", key)
    if ident and ident[0].isdigit():
        ident = "_" + ident
    return ident


def main():
    check_only = "--check" in sys.argv

    if not os.path.exists(MASTER):
        die("找不到 %s" % MASTER)
    with open(MASTER, encoding="utf-8") as f:
        data = json.load(f)

    locales = data.get("locales")
    strings = data.get("strings")
    if not locales or not strings:
        die("i18n_master.json 必須有 locales 與 strings")

    # ---- 驗證 locale 定義 ----
    codes = []
    primaries = []
    for loc in locales:
        for field in ("code", "name", "font", "rtl", "win_primary"):
            if field not in loc:
                die("locale %r 缺少欄位 %r" % (loc.get("code", "?"), field))
        if loc["code"] in codes:
            die("locale 代碼重複：%s" % loc["code"])
        codes.append(loc["code"])
        p = loc["win_primary"]
        if not isinstance(p, int) or not (0 <= p <= 0x3FF):
            die("locale %s 的 win_primary 不合法：%r" % (loc["code"], p))
        key = (p, loc.get("win_sublang", -1))
        if key in primaries:
            die("win_primary/sublang 重複：%r" % (key,))
        primaries.append(key)

    # ---- 驗證字串 ----
    errors = []
    n_keys = 0
    for key, entry in strings.items():
        n_keys += 1
        if not isinstance(entry, dict):
            errors.append("key %s 的值必須是 {locale: string}" % key)
            continue
        # 未知 locale
        for lc in entry:
            if lc not in codes:
                errors.append("key %s 有未知 locale %r" % (key, lc))
        # 基準格式符（以 zh-TW 或第一個有值的 locale 為準）
        base = None
        base_code = None
        for lc in codes:
            v = entry.get(lc)
            if isinstance(v, str) and v:
                try:
                    base = scan_fmt(v)
                except ValueError as e:
                    errors.append("key %s locale %s 格式字串不合法：%s" % (key, lc, e))
                    base = None
                base_code = lc
                break
        if base is None:
            if not any(isinstance(entry.get(lc), str) and entry.get(lc) for lc in codes):
                errors.append("key %s 在所有 locale 都是空的" % key)
            continue
        for lc in codes:
            v = entry.get(lc)
            if not isinstance(v, str) or not v:
                errors.append("key %s 缺少 locale %s 的翻譯" % (key, lc))
                continue
            for ch in v:
                o = ord(ch)
                # 允許 \t \n \r（多行字串用），其餘控制字元一律擋下
                if (o < 0x20 and ch not in "\t\n\r") or o == 0x7F:
                    errors.append("key %s locale %s 含控制字元 U+%04X" % (key, lc, o))
                    break
            try:
                got = scan_fmt(v)
            except ValueError as e:
                errors.append("key %s locale %s 格式字串不合法：%s" % (key, lc, e))
                continue
            if got != base:
                errors.append(
                    "key %s locale %s 格式符不符：基準(%s)=%s 實際=%s"
                    % (key, lc, base_code, base, got)
                )
    if errors:
        print("i18n 驗證失敗，共 %d 項：" % len(errors), file=sys.stderr)
        for e in errors[:60]:
            print("  - " + e, file=sys.stderr)
        if len(errors) > 60:
            print("  ...（其餘 %d 項省略）" % (len(errors) - 60), file=sys.stderr)
        sys.exit(1)

    print("i18n 驗證通過：%d 個 locale × %d 個字串 = %d 筆翻譯"
          % (len(codes), n_keys, len(codes) * n_keys))

    # ---- 資料完整性：en 基準不應含非 ASCII ----
    # 曾發生「補齊缺漏時誤把 zh-TW 寫進 en」的資料損毀，導致 en 與所有以英文
    # 暫代的語系都變成中文。en 是基準語言，正常情況下應全為 ASCII。
    nonascii = [(k, entry["en"]) for k, entry in strings.items()
                if isinstance(entry.get("en"), str)
                and any(ord(ch) > 127 for ch in entry["en"])]
    if nonascii:
        print("警告：下列 key 的 en 基準含非 ASCII 字元（可能是資料損毀）：", file=sys.stderr)
        for k, v in nonascii[:20]:
            print("  - %s = %r" % (k, v), file=sys.stderr)
        sys.exit(1)

    # ---- 資料完整性：每個 key 都必須有 en ----
    no_en = [k for k, e in strings.items() if not e.get("en")]
    if no_en:
        print("錯誤：下列 key 缺少 en 基準值：%s" % ", ".join(no_en[:20]), file=sys.stderr)
        sys.exit(1)

    if check_only:
        return

    # ---- 產生標頭 ----
    L = []
    L.append("/*")
    L.append(" * i18n_strings.h — 自動產生，請勿手改。")
    L.append(" *")
    L.append(" * 來源：tools/i18n_master.json")
    L.append(" * 產生：python3 tools/gen_i18n.py")
    L.append(" *")
    L.append(" * locale 數：%d，字串數：%d" % (len(codes), n_keys))
    L.append(" */")
    L.append("#ifndef ZL_I18N_STRINGS_H")
    L.append("#define ZL_I18N_STRINGS_H")
    L.append("")
    L.append("/* ---- locale 索引 ---- */")
    L.append("enum {")
    for i, loc in enumerate(locales):
        L.append("    LANG_%s = %d," % (c_ident(loc["code"]).upper(), i))
    L.append("    LANG_COUNT = %d" % len(locales))
    L.append("};")
    L.append("")
    L.append("/* ---- 字串 ID ---- */")
    L.append("enum {")
    for key in strings:
        L.append("    STR_%s," % c_ident(key))
    L.append("    STR_COUNT")
    L.append("};")
    L.append("")
    L.append("/* ---- locale 中介資料 ---- */")
    L.append("typedef struct {")
    L.append("    const wchar_t *code;       /* 標準化代碼，如 zh-TW / zh-CN */")
    L.append("    const wchar_t *endonym;    /* 該語言自稱，如 日本語 */")
    L.append("    const wchar_t *english;    /* 英文名 */")
    L.append("    const wchar_t *font;       /* 建議字型（找不到時自動回退） */")
    L.append("    int            rtl;        /* 是否由右至左 */")
    L.append("    int            win_primary;/* Win32 主要語言 ID */")
    L.append("    int            win_sublang;/* Win32 子語言 ID，-1 表示不限 */")
    L.append("} zl_lang_info;")
    L.append("")
    L.append("static const zl_lang_info g_lang_info[LANG_COUNT] = {")
    for loc in locales:
        L.append('    { L"%s", L"%s", L"%s", L"%s", %d, 0x%03X, %d },'
                 % (c_escape(loc["code"]), c_escape(loc["name"]),
                    c_escape(loc.get("english", loc["code"])), c_escape(loc["font"]),
                    1 if loc["rtl"] else 0, loc["win_primary"],
                    int(loc.get("win_sublang", -1))))
    L.append("};")
    L.append("")
    L.append("/* ---- 字串表 [locale][string id] ---- */")
    L.append("static const wchar_t *g_i18n[LANG_COUNT][STR_COUNT] = {")
    for loc in locales:
        lc = loc["code"]
        L.append("    { /* %s — %s */" % (lc, loc.get("english", "")))
        for key in strings:
            L.append('        L"%s",' % c_escape(strings[key][lc]))
        L.append("    },")
    L.append("};")
    L.append("")
    L.append("#endif /* ZL_I18N_STRINGS_H */")
    L.append("")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L))

    print("已產生 %s（%d 行，%d bytes）"
          % (os.path.relpath(OUT, ROOT), len(L), os.path.getsize(OUT)))


if __name__ == "__main__":
    main()
