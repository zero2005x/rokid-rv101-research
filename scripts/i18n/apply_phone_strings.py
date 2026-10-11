#!/usr/bin/env python3
"""Merge the 68 new strings produced by scripts/i18n into the phone app's 14 strings.xml files.

Input:
  scripts/i18n/phone_keys.tsv          key / zh-TW / en (zh-TW -> values/, en -> values-en/)
  scripts/i18n/out/*.xml               `@@values-xx` blocks (the other 12 locales)
Output:
  android/app-phone/src/main/res/values-*/strings.xml with the new strings inserted before
  </resources>

Behaviour: idempotent (an existing key is removed before being written again); XML escaping
(& ' < >) happens before insertion.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path("/home/kali/rokid_root")
RES = ROOT / "android/app-phone/src/main/res"
KEYS_TSV = ROOT / "scripts/i18n/phone_keys.tsv"
OUT_DIR = ROOT / "scripts/i18n/out"

# locale tag -> res directory
DIR_OF = {
    "zh-TW": "values",
    "zh-CN": "values-zh-rCN",
    "en": "values-en",
    "ja": "values-ja",
    "ko": "values-ko",
    "fr": "values-fr",
    "de": "values-de",
    "es": "values-es",
    "pt": "values-pt",
    "ru": "values-ru",
    "uk": "values-uk",
    "th": "values-th",
    "vi": "values-vi",
    "ar": "values-ar",
}


def esc(v: str) -> str:
    """Escape for an Android string resource (preserving existing backslash sequences such as \\n)."""
    v = re.sub(r"&(?!(?:amp|lt|gt|quot|apos|#\d+|#x[0-9A-Fa-f]+);)", "&amp;", v)
    v = v.replace("<", "&lt;").replace(">", "&gt;")
    out, i = [], 0
    while i < len(v):
        c = v[i]
        if c == "\\" and i + 1 < len(v):      # keep \n \' \" and the like
            out.append(v[i:i + 2]); i += 2; continue
        out.append("\\'" if c == "'" else c)
        i += 1
    return "".join(out)


def load_keys() -> list[str]:
    lines = KEYS_TSV.read_text(encoding="utf-8").splitlines()
    assert lines[0].split("\t")[:3] == ["key", "zh-TW", "en"], "TSV 表頭不符"
    keys = []
    for ln in lines[1:]:
        if not ln.strip():
            continue
        parts = ln.split("\t")
        assert len(parts) == 3, f"TSV 欄位數不符: {ln!r}"
        keys.append(parts[0])
    return keys


def load_translations() -> dict[str, dict[str, str]]:
    """locale tag -> {key: value} (zh-TW / en come from the TSV, the rest from out/*.xml)."""
    tr: dict[str, dict[str, str]] = {t: {} for t in DIR_OF}
    for ln in KEYS_TSV.read_text(encoding="utf-8").splitlines()[1:]:
        if not ln.strip():
            continue
        k, zh, en = ln.split("\t")
        tr["zh-TW"][k] = zh
        tr["en"][k] = en
    dir2tag = {v: k for k, v in DIR_OF.items()}
    for f in sorted(OUT_DIR.glob("*.xml")):
        cur = None
        for ln in f.read_text(encoding="utf-8").splitlines():
            s = ln.strip()
            if s.startswith("@@"):
                tag = s[2:].strip()
                # both `@@fr` (locale tag) and `@@values-fr` (res directory name) are accepted
                tag = dir2tag.get(tag, tag)
                assert tag in DIR_OF, f"{f.name}: 未知 locale tag {s}"
                cur = tag
                continue
            if not s:
                continue
            m = re.match(r'<string name="([^"]+)">(.*)</string>$', s)
            assert m and cur, f"{f.name}: 無法解析 {s[:60]!r}"
            tr[cur][m.group(1)] = m.group(2)
    return tr


def main() -> int:
    keys = load_keys()
    tr = load_translations()
    problems = []
    for tag, vals in tr.items():
        missing = [k for k in keys if k not in vals]
        extra = [k for k in vals if k not in keys]
        if missing or extra:
            problems.append(f"{tag}: missing={missing} extra={extra}")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1

    for tag, d in DIR_OF.items():
        p = RES / d / "strings.xml"
        s = p.read_text(encoding="utf-8")
        for k in keys:  # idempotent: remove a pre-existing key of the same name first
            s = re.sub(rf'\n?\s*<string name="{re.escape(k)}">.*?</string>', "", s)
        block = "\n".join(f'    <string name="{k}">{esc(tr[tag][k])}</string>' for k in keys)
        s = s.replace("</resources>", f"\n    <!-- i18n：操作紀錄／提示訊息（scripts/i18n 產出） -->\n{block}\n</resources>")
        p.write_text(s, encoding="utf-8")
        print(f"OK   {tag:6s} -> {d}/strings.xml  (+{len(keys)} keys)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
