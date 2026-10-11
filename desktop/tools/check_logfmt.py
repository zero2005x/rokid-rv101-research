#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_logfmt.py — statically compare the "argument count" of every localised formatting call in
the C source against the "specifier count" in the string table.

Why it is needed: log_msg()/stat_msg()/Tf() all wrap vswprintf, and the format strings come from a
runtime lookup. If a call site supplies one argument too few (or too many), vswprintf reads
non-existent stack contents — a bug that crashes outright and that the compiler **cannot** check
(the format string is not a literal). This script fills that gap.

Usage:
  python3 tools/check_logfmt.py            # check src/zero_light_desktop.c
  python3 tools/check_logfmt.py FILE ...   # check the given files
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MASTER = os.path.join(ROOT, "tools", "i18n_master.json")

# printf specifiers (same definition as gen_i18n.py)
FMT_RE = re.compile(
    r"%(?:[-+ #0]*)(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:hh|h|ll|l|L|z|j|t|I64|I32)?([diouxXeEfgGaAcspn%])"
)


def count_specs(s):
    """Return the number of specifiers that consume an argument. %% consumes none; a stray % is
    an error."""
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
    Parse from just after s[start] (i.e. after '(') to the matching ')' and return (list of
    top-level arguments, index of the closing bracket). Handles nested brackets, strings and
    character literals correctly.
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

    # Second class: the printf family with a "literal" format string.
    # In principle -Wformat would check these, but GCC does not treat MinGW's __mingw_swprintf
    # and friends as printf prototypes and was measured to miss them — code with 14 %ls but only
    # 11 arguments once passed, then APPCRASHed in msvcrt.dll at runtime.
    lit_re = re.compile(
        r"\b(_?s?n?w?printf|_snwprintf|_snprintf|swprintf|snprintf|sprintf|wsprintfW?)\s*\(")
    problems = []
    checked = 0

    for path in files:
        src = open(path, encoding="utf-8").read()
        # Strip noise outside comments and string literals: remove /* */ and // comments first so
        # examples inside documentation comments are not taken for real calls.
        src = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"),
                     src, flags=re.S)
        src = re.sub(r"//[^\n]*", "", src)
        for m in call_re.finditer(src):
            func, enum = m.group(1), m.group(2)
            key = enum[4:]                      # drop the STR_ prefix
            line = src.count("\n", 0, m.start()) + 1

            if key not in strings:
                problems.append((path, line, func, enum,
                                 "字串表中沒有這個 key", None, None))
                continue

            # m.end() is right after the id; split_args counts the "empty fragment before the
            # first comma" as an element too, so the real argument count is len(args) - 1.
            args, _ = split_args(src, m.end())
            if args is None:
                problems.append((path, line, func, enum, "無法解析引數（括號未平衡）", None, None))
                continue
            given = len(args) - 1

            # use en as the baseline (gen_i18n.py guarantees every locale has the same specifiers)
            try:
                need = count_specs(strings[key]["en"])
            except ValueError as e:
                problems.append((path, line, func, enum, "格式字串不合法：%s" % e, None, None))
                continue

            checked += 1
            if given != need:
                problems.append((path, line, func, enum,
                                 "引數個數不符", given, need))

    # ---- printf family with literal format strings ----
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
            # the format string is the 3rd argument (buf, size, fmt, ...)
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

    # ---- Third class: T() fetching a string that contains specifiers without supplying
    # arguments ----
    # T() only looks up, it does not format, so T(STR_APP_TITLE) shows "v%ls" verbatim (measured:
    # the dialog title became "ZeroLightRokid Desktop v%ls — ..."). The compiler cannot see this
    # class of mistake at all, because the format string is not a literal.
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

    # ---- Fourth class: %s misused in a wide format string (under MinGW ANSI stdio %s = char*) ----
    # This is the nastiest class: it compiles and runs, the string is just truncated.
    # Measured: swprintf(v, 24, L"v%s", L"3.0.0") yields only "v3".
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
            # (?<!%) skips an escaped %%s — in C that prints the literal "%s", e.g. the shell's
            # `date +%s` is deliberately written as %%s and must not be flagged.
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
