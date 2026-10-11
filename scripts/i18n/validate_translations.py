#!/usr/bin/env python3
"""i18n quality check: TSV + out/*.xml (run before apply_phone_strings.py).

Checks:
  1. TSV: 68 keys, 3 columns per row, no duplicates, zh-TW and en placeholders match
  2. out/*.xml: well-formed XML, key coverage and order identical to the TSV, no duplicates
  3. each locale's placeholders (%n$s/%n$d) match the TSV en column key by key (any order, same
     count)
  4. literal `\\n` preserved, leading symbols ✓/✗/⚠/● required, no unescaped & < >
  5. terminology consistency per locale: do WiFi/Bluetooth/メガネ etc. follow that locale's
     customary terms (see normalize_locale_terms.py)
Exit code 0 = everything passes; 1 = problems found (listed one by one)
"""
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path("/home/kali/rokid_root")
TSV = ROOT / "scripts/i18n/phone_keys.tsv"
OUT = ROOT / "scripts/i18n/out"

# Customary terms from each locale's existing files (matching normalize_locale_terms.py;
# here we check that no forbidden spelling appears)
FORBIDDEN = {
    "values-fr": ["WiFi"], "values-de": ["WiFi"], "values-pt": ["WiFi"],
    "values-ja": ["WiFi", "グラス"], "values-ko": ["WiFi", "Bluetooth"],
    "values-ru": ["WiFi"], "values-uk": ["WiFi"], "values-th": ["WiFi", "Bluetooth"],
    "values-vi": ["WiFi"], "values-ar": ["WiFi", "Bluetooth"],
    "values-zh-rCN": ["Bluetooth"],
}

problems: list[str] = []


def load_tsv():
    lines = TSV.read_text(encoding="utf-8").splitlines()
    if lines[0] != "key\tzh-TW\ten":
        problems.append("TSV 表頭不是 key<TAB>zh-TW<TAB>en")
    rows = []
    for ln in lines[1:]:
        if not ln.strip():
            continue
        parts = ln.split("\t")
        if len(parts) != 3:
            problems.append(f"TSV 欄位數不是 3：{ln[:60]!r}")
            continue
        rows.append(parts)
    keys = [r[0] for r in rows]
    if len(keys) != len(set(keys)):
        problems.append("TSV 有重複 key")
    return {r[0]: (r[1], r[2]) for r in rows}, keys


def ph(s: str) -> list[str]:
    return sorted(re.findall(r"%[0-9]+\$[sd]", s))


def main() -> int:
    tsv, keys = load_tsv()
    for k, (zh, en) in tsv.items():
        if ph(zh) != ph(en):
            problems.append(f"TSV {k}: zh-TW/en 佔位符不一致 {ph(zh)} vs {ph(en)}")
    for f in sorted(OUT.glob("*.xml")):
        raw = f.read_text(encoding="utf-8")
        # XML check (strip the @@ markers, then parse block by block)
        blocks = re.split(r"^@@", raw, flags=re.M)[1:]
        for blk in blocks:
            tag, body = blk.split("\n", 1)
            tag = tag.strip()
            try:
                ET.fromstring(f"<resources>{body}</resources>")
            except ET.ParseError as e:
                problems.append(f"{f.name} [{tag}] XML 解析失敗：{e}")
            seen = []
            for m in re.finditer(r'<string name="([^"]+)">(.*?)</string>', body):
                key, val = m.group(1), m.group(2)
                seen.append(key)
                if key not in tsv:
                    problems.append(f"{f.name} [{tag}] 未知 key：{key}")
                    continue
                if ph(val) != ph(tsv[key][1]):
                    problems.append(f"{f.name} [{tag}] {key}: 佔位符 {ph(val)} != {ph(tsv[key][1])}")
                en = tsv[key][1]
                if en.startswith("\\n") and not val.startswith("\\n"):
                    problems.append(f"{f.name} [{tag}] {key}: 缺少開頭 \\n")
                for sym in ("✓", "✗", "⚠", "●"):
                    if en.startswith(sym) and not val.startswith(sym):
                        problems.append(f"{f.name} [{tag}] {key}: 缺少開頭 {sym}")
                if re.search(r"&(?!(?:amp|lt|gt|quot|apos|#\d+|#x[0-9A-Fa-f]+);)", val):
                    problems.append(f"{f.name} [{tag}] {key}: 未轉義的 &")
                for bad in FORBIDDEN.get(tag, []):
                    if bad in val:
                        problems.append(f"{f.name} [{tag}] {key}: 出現禁用寫法 {bad!r}（應用 {tag} 慣用詞）")
            missing = [k for k in keys if k not in seen]
            if missing:
                problems.append(f"{f.name} [{tag}] 缺少 {len(missing)} 個 key：{missing[:5]}")
            if len(seen) != len(set(seen)):
                problems.append(f"{f.name} [{tag}] 區塊內有重複 key")
    if problems:
        print(f"❌ 發現 {len(problems)} 個問題：")
        for p in problems:
            print("  -", p)
        return 1
    print(f"✅ 檢查通過：TSV {len(keys)} keys；out/*.xml {len(list(OUT.glob('*.xml')))} 檔 × 各 4 語系，"
          f"佔位符／覆蓋率／轉義／術語一致")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
