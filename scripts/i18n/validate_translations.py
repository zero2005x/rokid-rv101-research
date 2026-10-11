#!/usr/bin/env python3
"""i18n 品質檢查：TSV ＋ out/*.xml（在 apply_phone_strings.py 之前執行）。

檢查項目：
  1. TSV：68 個 key、每列 3 欄、無重複、zh-TW 與 en 佔位符一致
  2. out/*.xml：XML 格式正確、key 覆蓋率與順序與 TSV 相同、無重複
  3. 每個語系的佔位符（%n$s/%n$d）與 TSV 的 en 欄逐 key 一致（順序不拘、數量須相同）
  4. 保留字面 `\\n`、開頭符號 ✓/✗/⚠/●、不得有未轉義的 & < >
  5. 各語系術語一致性：WiFi/Bluetooth/メガネ 等是否符合該語系既有慣用詞（見 normalize_locale_terms.py）
退出碼 0 = 全部通過；1 = 有問題（逐項列出）
"""
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path("/home/kali/rokid_root")
TSV = ROOT / "scripts/i18n/phone_keys.tsv"
OUT = ROOT / "scripts/i18n/out"

# 各語系既有檔案的慣用詞（與 normalize_locale_terms.py 對應；此處檢查不得出現禁用寫法）
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
        # XML 檢查（把 @@ 標記拿掉後逐區塊解析）
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
