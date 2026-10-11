#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_logfmt.py — 靜態比對 C 原始碼中每個多語系格式化呼叫的「引數個數」與
                 字串表格式符的「格式符個數」。

為什麼需要：log_msg()/stat_msg()/Tf() 都是 vswprintf 的包裝，格式字串來自
執行期查表。若呼叫端少給一個引數（或多給），vswprintf 會讀取不存在的堆疊
內容 —— 這是會直接崩潰的錯誤，而編譯器**無法**檢查（格式字串不是字面值）。
本腳本補上這個缺口。

用法：
  python3 tools/check_logfmt.py            # 檢查 src/zero_light_desktop.c
  python3 tools/check_logfmt.py FILE ...   # 檢查指定檔案
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MASTER = os.path.join(ROOT, "tools", "i18n_master.json")

# printf 格式符（與 gen_i18n.py 相同的定義）
FMT_RE = re.compile(
    r"%(?:[-+ #0]*)(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:hh|h|ll|l|L|z|j|t|I64|I32)?([diouxXeEfgGaAcspn%])"
)


def count_specs(s):
    """回傳 (需要引數的格式符個數)。%% 不消耗引數。落單的 % 視為錯誤。"""
    n = 0
    i = 0
    while i < len(s):
        if s[i] != "%":
            i += 1
            continue
        m = FMT_RE.match(s, i)
        if not m:
            raise ValueError("落單的 %% 於 %r" % s[max(0, i - 10):i + 10])
        if m.group(1) != "%":
            n += 1
        i = m.end()
    return n


def split_args(s, start):
    """
    從 s[start] 之後（即 '(' 之後）解析到對應的 ')'，
    回傳 (頂層引數清單, 收尾括號索引)。正確處理巢狀括號、字串與字元常值。
    """
    args = []
    depth = 0
    cur = []
    i = start
    in_str = in_chr = False
    while i < len(s):
        c = s[i]
        if in_str:
            cur.append(c)
            if c == "\\":
                i += 1
                if i < len(s):
                    cur.append(s[i])
            elif c == '"':
                in_str = False
        elif in_chr:
            cur.append(c)
            if c == "\\":
                i += 1
                if i < len(s):
                    cur.append(s[i])
            elif c == "'":
                in_chr = False
        elif c == '"':
            in_str = True
            cur.append(c)
        elif c == "'":
            in_chr = True
            cur.append(c)
        elif c in "([{":
            depth += 1
            cur.append(c)
        elif c in ")]}":
            if depth == 0:
                args.append("".join(cur).strip())
                return args, i
            depth -= 1
            cur.append(c)
        elif c == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    return None, -1


def main():
    files = sys.argv[1:] or [os.path.join(ROOT, "src", "zero_light_desktop.c")]
    with open(MASTER, encoding="utf-8") as f:
        data = json.load(f)
    strings = data["strings"]

    call_re = re.compile(r"\b(log_msg|stat_msg|Tf)\s*\(\s*(STR_[A-Z0-9_]+)")

    # 第二類：格式字串是「字面值」的 printf 家族。
    # 這一類照理編譯器 -Wformat 會檢查，但 MinGW 的 __mingw_swprintf 等函式
    # 未被 GCC 視為 printf 原型，實測漏檢 —— 曾因此讓 14 個 %ls 配 11 個引數
    # 的程式碼過關，執行時 APPCRASH 於 msvcrt.dll。
    lit_re = re.compile(
        r"\b(_?s?n?w?printf|_snwprintf|_snprintf|swprintf|snprintf|sprintf|wsprintfW?)\s*\(")
    problems = []
    checked = 0

    for path in files:
        src = open(path, encoding="utf-8").read()
        # 移除註解與字串常值之外的雜訊：先移除 /* */ 與 // 註解，
        # 避免文件註解中的範例被當成真實呼叫。
        src = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"),
                     src, flags=re.S)
        src = re.sub(r"//[^\n]*", "", src)
        for m in call_re.finditer(src):
            func, enum = m.group(1), m.group(2)
            key = enum[4:]                      # 去掉 STR_
            line = src.count("\n", 0, m.start()) + 1

            if key not in strings:
                problems.append((path, line, func, enum,
                                 "字串表中沒有這個 key", None, None))
                continue

            # m.end() 緊接在 id 之後；split_args 會把「第一個逗號前的空片段」
            # 也算成一個元素，因此實際引數個數 = len(args) - 1。
            args, _ = split_args(src, m.end())
            if args is None:
                problems.append((path, line, func, enum, "無法解析引數（括號未平衡）", None, None))
                continue
            given = len(args) - 1

            # 以 en 為基準（gen_i18n.py 已保證所有語系的格式符與 en 相同）
            try:
                need = count_specs(strings[key]["en"])
            except ValueError as e:
                problems.append((path, line, func, enum, "格式字串不合法：%s" % e, None, None))
                continue

            checked += 1
            if given != need:
                problems.append((path, line, func, enum,
                                 "引數個數不符", given, need))

    # ---- 字面格式字串的 printf 家族 ----
    lit_checked = 0
    for path in files:
        raw = open(path, encoding="utf-8").read()
        raw = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), raw, flags=re.S)
        raw = re.sub(r"//[^\n]*", "", raw)
        for m in lit_re.finditer(raw):
            func = m.group(1)
            line = raw.count("\n", 0, m.start()) + 1
            args, _ = split_args(raw, m.end() - 1) if False else split_args(raw, m.end())
            if args is None or len(args) < 4:
                continue
            # 格式字串是第 3 個引數（buf, size, fmt, ...）
            fmt = args[2].strip()
            if not (fmt.startswith('L"') or fmt.startswith('"')):
                continue
            body = "".join(re.findall(r'L?"([^"\\]*(?:\\.[^"\\]*)*)"', fmt))
            try:
                need = count_specs(body)
            except ValueError as e:
                problems.append((path, line, func, "<literal>", "格式字串不合法：%s" % e, None, None))
                continue
            given = len(args) - 3
            lit_checked += 1
            if given != need:
                problems.append((path, line, func, "<literal>", "引數個數不符", given, need))

    # ---- 第三類：用 T() 取「含格式符」的字串卻沒有代入引數 ----
    # T() 只查表不格式化，所以 T(STR_APP_TITLE) 會把 "v%ls" 原樣顯示出來
    # （實測：對話框標題變成 "ZeroLightRokid Desktop v%ls — …"）。這類失誤
    # 編譯器完全看不到，因為格式字串不是字面值。
    t_re = re.compile(r"(?<!\w)T\s*\(\s*(STR_[A-Z0-9_]+)\s*\)")
    t_checked = 0
    for path in files:
        raw = open(path, encoding="utf-8").read()
        raw = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), raw, flags=re.S)
        raw = re.sub(r"//[^\n]*", "", raw)
        for m in t_re.finditer(raw):
            key = m.group(1)[4:]
            if key not in strings:
                continue
            line = raw.count("\n", 0, m.start()) + 1
            t_checked += 1
            try:
                need = count_specs(strings[key]["en"])
            except ValueError:
                continue
            if need > 0:
                problems.append((path, line, "T", m.group(1),
                                 "字串含格式符但未用 Tf() 代入引數（會顯示字面格式符）",
                                 None, need))

    # ---- 第四類：寬格式字串中誤用 %s（在 MinGW ANSI stdio 下 %s = char*）----
    # 這是最陰險的一類：程式可編譯、可執行，只是字串被截斷。
    # 實測踩到：swprintf(v, 24, L"v%s", L"3.0.0") 只會得到 "v3"。
    wide_re = re.compile(r"\b(swprintf|_snwprintf|wprintf|fwprintf|vswprintf)\s*\(")
    wide_checked = 0
    for path in files:
        raw = open(path, encoding="utf-8").read()
        raw = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), raw, flags=re.S)
        raw = re.sub(r"//[^\n]*", "", raw)
        for m in wide_re.finditer(raw):
            args, _ = split_args(raw, m.end())
            if not args:
                continue
            fmts = [a for a in args if a.strip().startswith('L"')]
            if not fmts:
                continue
            body = "".join(re.findall(r'L?"([^"\\]*(?:\\.[^"\\]*)*)"', fmts[0]))
            line = raw.count("\n", 0, m.start()) + 1
            wide_checked += 1
            # 用 (?<!%) 避開跳脫的 %%s —— 那在 C 裡輸出的是字面 "%s"，
            # 例如 shell 的 `date +%s` 就是刻意寫成 %%s，不該被當成錯誤。
            bad = re.findall(r"(?<!%)%(?!l)[-+ #0-9.*]*s", body)
            if bad:
                problems.append((path, line, m.group(1), "<wide-fmt>",
                                 "寬格式字串用了 %%s（MinGW 下為 char*，會截斷）；應用 %%ls",
                                 None, len(bad)))

    print("已檢查 %d 個多語系格式化呼叫、%d 個字面格式呼叫、%d 個 T() 取用、%d 個寬格式字串"
          % (checked, lit_checked, t_checked, wide_checked))
    if problems:
        print("\n發現 %d 個問題：" % len(problems), file=sys.stderr)
        for path, line, func, enum, why, given, need in problems:
            loc = "%s:%d" % (os.path.relpath(path, ROOT), line)
            if given is None:
                print("  - %s  %s(%s) → %s" % (loc, func, enum, why), file=sys.stderr)
            else:
                print("  - %s  %s(%s) → %s：呼叫端給 %d 個，格式字串需要 %d 個"
                      % (loc, func, enum, why, given, need), file=sys.stderr)
        return 1
    print("✓ 所有呼叫的引數個數都與格式字串相符")
    return 0


if __name__ == "__main__":
    sys.exit(main())
