#!/usr/bin/env python3
"""把 scripts/i18n 產出的 68 個新字串併入 app-phone 的 14 個 strings.xml。

輸入：
  scripts/i18n/phone_keys.tsv          key / zh-TW / en（zh-TW → values/，en → values-en/）
  scripts/i18n/out/*.xml               `@@values-xx` 區塊（其餘 12 語系）
輸出：
  android/app-phone/src/main/res/values-*/strings.xml 於 </resources> 前插入新字串

特性：可重複執行（已存在的 key 會先移除再寫入）；插入前做 XML 轉義（& ' < >）。
"""
import pathlib
import re
import sys

ROOT = pathlib.Path("/home/kali/rokid_root")
RES = ROOT / "android/app-phone/src/main/res"
KEYS_TSV = ROOT / "scripts/i18n/phone_keys.tsv"
OUT_DIR = ROOT / "scripts/i18n/out"

# locale tag → res 目錄
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
    """Android 字串資源轉義（保留既有 \\n 等反斜線序列）。"""
    v = re.sub(r"&(?!(?:amp|lt|gt|quot|apos|#\d+|#x[0-9A-Fa-f]+);)", "&amp;", v)
    v = v.replace("<", "&lt;").replace(">", "&gt;")
    out, i = [], 0
    while i < len(v):
        c = v[i]
        if c == "\\" and i + 1 < len(v):      # 保留 \n \' \" 等
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
    """locale tag → {key: value}（zh-TW / en 取自 TSV，其餘取自 out/*.xml）。"""
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
                # 允許 `@@fr`（locale tag）或 `@@values-fr`（res 目錄名）兩種寫法
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
        for k in keys:  # 可重複執行：先移除既有同名 key
            s = re.sub(rf'\n?\s*<string name="{re.escape(k)}">.*?</string>', "", s)
        block = "\n".join(f'    <string name="{k}">{esc(tr[tag][k])}</string>' for k in keys)
        s = s.replace("</resources>", f"\n    <!-- i18n：操作紀錄／提示訊息（scripts/i18n 產出） -->\n{block}\n</resources>")
        p.write_text(s, encoding="utf-8")
        print(f"OK   {tag:6s} -> {d}/strings.xml  (+{len(keys)} keys)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
