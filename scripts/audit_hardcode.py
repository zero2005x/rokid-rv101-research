#!/usr/bin/env python3
"""硬編碼稽核：掃描兩個 App、shared 與腳本，找出不該寫死的東西。

檢查項目：
  1. Kotlin 中文字面值（排除註解）→ 可能是未在地化的使用者可見文字
  2. Kotlin 硬編碼路徑（/data、/sdcard、/sys…）、IP、MAC、埠號
  3. 版面 XML 硬編碼 android:text/hint/contentDescription 與顏色
  4. 重複出現的字面值（同一常數散落多處 → 應集中成常數）
  5. deploy/、scripts/ 內硬編碼的序號、IP、Windows 路徑
  6. Android 元件可達性：`android:exported="true"` 而**沒有** `android:permission`
  7. 憑證型字面值（pass／secret／token／key／pin 直接寫死）
  8. native 端權限（world-accessible chmod）

第 6–8 項是 2026-09-13 的戒指稽核後補上的：先前 `DebugReceiver` 被歸類為
「開發工具訊息」而漏掉它的**可達性**（任何 App 都能送），`AP_PASS` 也從未被列出。
見 reports/ring_capture_security_audit_2026-09-13.md §2 R4/R5/R8。

用法：python3 scripts/audit_hardcode.py [--only N]
"""
import argparse
import collections
import pathlib
import re
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent.parent
SKIP_DIRS = {"build", ".gradle", ".git", "jadx", "jadx_out", "node_modules", "out", "reports"}

CJK = re.compile(r'"(?:[^"\\]|\\.)*[\u4e00-\u9fff](?:[^"\\]|\\.)*"')
PAT_PATH = re.compile(r'"(/(?:data|sdcard|storage|sys|proc|dev|system)[^"\s]*)"')
PAT_IP = re.compile(r'"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})"')
PAT_MAC = re.compile(r'\b([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})\b')
# 只在 SERIAL= 或 adb -s 的參數位置判斷，且值需含 8 位以上數字（避免誤判程序名）
PAT_SERIAL = re.compile(r'(?:SERIAL=\s*"?|\s-s\s+"?)(?=[0-9A-Za-z]{8,20}"?)(?=[0-9A-Za-z]*[0-9]{8})([0-9A-Za-z]{8,20})')
PAT_STR = re.compile(r'"((?:[^"\\]|\\.){4,60})"')
# 憑證型字面值：識別字含 pass/secret/token/key/pin，且直接賦值成字串
PAT_CRED = re.compile(
    r'(?i)\b([A-Za-z_][A-Za-z0-9_]*)\s*[:=]\s*"([^"\n]{1,64})"'
)
# 識別字本身即為憑證名（無條件算）
PAT_CRED_ALWAYS = re.compile(
    r'(?i)(?:^|_)(?:pass|passwd|password|pwd|secret|token|pin)(?:$|_)'
    r'|(?:^|_)api_?key(?:$|_)|pass(?:word|wd)?$|secret|token'
)
# 以 key 結尾者：只有值「像金鑰」才算（排除 lastKey / readerOffset 之類）
PAT_CRED_KEYNAME = re.compile(r'(?i)(?:^|_)key$|Key$|_key_?$')
PAT_KEYLIKE = re.compile(r'(?:[0-9a-fA-F]{16,}|[A-Za-z0-9+/]{20,}={0,2})')

# 受系統保護的廣播（只有系統能送；這些 intent-filter 不需 permission）
PROTECTED_ACTIONS = {
    "android.intent.action.BOOT_COMPLETED",
    "android.intent.action.LOCKED_BOOT_COMPLETED",
    "android.intent.action.MY_PACKAGE_REPLACED",
    "android.intent.action.PACKAGE_REPLACED",
    "android.intent.action.TIMEZONE_CHANGED",
    "android.intent.action.TIME_SET",
    "android.intent.action.DATE_CHANGED",
    "android.intent.action.USER_PRESENT",
    "android.intent.action.POWER_CONNECTED",
    "android.intent.action.POWER_DISCONNECTED",
    "android.intent.action.LOCALE_CHANGED",
}
# native：chmod(path, mode)
PAT_CHMOD = re.compile(r'chmod\s*\(\s*[^,]+,\s*(0?[0-7]{3,4})\s*\)')

ANDROID_NS = "{http://schemas.android.com/apk/res/android}"
COMPONENT_TAGS = ("activity", "activity-alias", "service", "receiver", "provider")



def files(suffixes, roots):
    for r in roots:
        base = ROOT / r
        if not base.exists():
            continue
        for p in base.rglob("*"):
            if not p.is_file() or any(s in p.parts for s in SKIP_DIRS):
                continue
            if p.suffix in suffixes:
                yield p


def kotlin_files():
    return sorted(files({".kt"}, ["android"]))


def is_comment(line: str) -> bool:
    s = line.strip()
    return s.startswith(("//", "*", "/*", "*/"))


def section(title: str, n: int, only: int | None):
    if only in (None, n):
        print("=" * 72)
        print(f"{n}) {title}")
        print("=" * 72)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", type=int, default=None)
    args = ap.parse_args()
    only = args.only

    # 1) 中文字面值
    section("Kotlin 中文字面值（可能是未在地化的使用者可見文字）", 1, only)
    if only in (None, 1):
        for p in kotlin_files():
            hits = [(i, m.group(0)) for i, ln in enumerate(p.read_text(encoding="utf-8").splitlines(), 1)
                    if not is_comment(ln) for m in CJK.finditer(ln)]
            if hits:
                print(f"\n{p.relative_to(ROOT)}  （{len(hits)} 筆）")
                for i, h in hits[:8]:
                    print(f"  {i:4d}: {h[:88]}")
                if len(hits) > 8:
                    print(f"  … 其餘 {len(hits) - 8} 筆")

    # 2) 路徑／IP／MAC／埠
    section("硬編碼路徑／IP／MAC／埠號（Kotlin）", 2, only)
    if only in (None, 2):
        for name, pat in (("路徑", PAT_PATH), ("IP", PAT_IP), ("MAC", PAT_MAC)):
            counter, where = collections.Counter(), collections.defaultdict(set)
            for p in kotlin_files():
                for ln in p.read_text(encoding="utf-8").splitlines():
                    if is_comment(ln):
                        continue
                    for m in pat.finditer(ln):
                        v = m.group(1)
                        counter[v] += 1
                        where[v].add(p.name)
            if counter:
                print(f"\n[{name}]")
                for v, c in counter.most_common(15):
                    print(f"  {v:44s} ×{c:<3} {sorted(where[v])[:4]}")

    # 3) 版面 XML
    section("版面 XML：硬編碼文字與顏色", 3, only)
    if only in (None, 3):
        hard, colors = [], collections.Counter()
        for p in files({".xml"}, ["android/app-phone/src/main/res/layout",
                                  "android/app-glasses/src/main/res/layout"]):
            for i, ln in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
                s = ln.strip()
                if re.search(r'android:(text|hint|contentDescription)="(?!@string)', s):
                    hard.append((p, i, s[:96]))
                for m in re.finditer(r'"#([0-9A-Fa-f]{6,8})"', s):
                    colors[m.group(0)] += 1
        print(f"硬編碼文字：{len(hard)} 筆")
        for p, i, s in hard[:20]:
            print(f"  {p.name}:{i}  {s}")
        print(f"\n硬編碼顏色：{sum(colors.values())} 筆")
        for v, c in colors.most_common(12):
            print(f"  {v} ×{c}")

    # 4) 重複字面值
    section("重複字面值（散落多處 → 建議集中為常數）", 4, only)
    if only in (None, 4):
        counter, where = collections.Counter(), collections.defaultdict(set)
        for p in kotlin_files():
            for ln in p.read_text(encoding="utf-8").splitlines():
                if is_comment(ln):
                    continue
                for m in PAT_STR.finditer(ln):
                    v = m.group(1)
                    if v.startswith(("android.", "com.", "http")) or " " in v:
                        continue
                    counter[v] += 1
                    where[v].add(p.name)
        for v, c in counter.most_common(25):
            if c >= 3 and len(where[v]) >= 2:
                print(f"  {v:46s} ×{c:<3} {sorted(where[v])[:5]}")

    # 5) 腳本
    section("腳本硬編碼（序號／IP／Windows 路徑）", 5, only)
    if only in (None, 5):
        for p in files({".sh", ".cmd", ".ps1"}, ["deploy", "scripts"]):
            hits = []
            for i, ln in enumerate(p.read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
                if ln.strip().startswith("#"):
                    continue
                for pat, tag in ((PAT_IP, "IP"), (PAT_SERIAL, "序號")):
                    for m in pat.finditer(ln):
                        hits.append((i, tag, m.group(1)))
                if "/mnt/c/" in ln:
                    hits.append((i, "Windows路徑", ln.strip()[:80]))
            if hits:
                print(f"\n{p.relative_to(ROOT)}")
                for i, tag, v in hits[:6]:
                    print(f"  {i:4d} [{tag}] {v}")

    # 6) Android 元件可達性（exported 且無 permission）
    section("Android 元件可達性：exported=true 而無 android:permission（任何 App 可觸發）", 6, only)
    if only in (None, 6):
        total = 0
        for p in sorted(files({".xml"}, ["android"])):
            if p.name != "AndroidManifest.xml":
                continue
            try:
                root = ET.parse(p).getroot()
            except ET.ParseError as e:
                print(f"\n{p.relative_to(ROOT)}  ⚠ 無法解析：{e}")
                continue
            rows = []
            for comp in root.iter():
                tag = comp.tag.split("}")[-1]
                if tag not in COMPONENT_TAGS:
                    continue
                name = comp.get(ANDROID_NS + "name", "?")
                exported = comp.get(ANDROID_NS + "exported")
                perm = comp.get(ANDROID_NS + "permission")
                has_filter = comp.find("intent-filter") is not None
                is_launcher = any(
                    (c.get(ANDROID_NS + "name") or "") == "android.intent.category.LAUNCHER"
                    for f in comp.findall("intent-filter") for c in f.findall("category")
                )
                actions = {
                    a.get(ANDROID_NS + "name") or ""
                    for f in comp.findall("intent-filter")
                    for a in f.findall("action")
                }
                protected = bool(actions) and actions <= PROTECTED_ACTIONS
                if exported == "true" and perm is None:
                    if is_launcher and tag == "activity":
                        note = "（launcher／預期）"
                    elif protected:
                        note = "（僅受系統保護廣播／預期）"
                    else:
                        note = "⚠ 無 permission"
                    rows.append((name, tag, note))
                elif exported is None and has_filter:
                    rows.append((name, tag, "⚠ 有 intent-filter 但未宣告 exported（API 31+ 需明示）"))
            if rows:
                print(f"\n{p.relative_to(ROOT)}")
                for name, tag, note in rows:
                    print(f"  {tag:10s} {name:46s} {note}")
                total += len(rows)
        print(f"\n合計 {total} 個元件需檢視（launcher activity 屬正常）。")
        print("  判準：exported=true 且無 android:permission ⇒ 任何已安裝 App 都能啟動／投遞。")
        print("  若要保留 `adb shell am broadcast` 自動化，可加 android:permission=\"android.permission.DUMP\"")
        print("  （shell 持有 DUMP，第三方 App 沒有）。")

    # 7) 憑證型字面值
    section("憑證型字面值（密碼／權杖／金鑰直接寫死）", 7, only)
    if only in (None, 7):
        hits = []
        for p in files({".kt", ".java", ".c", ".h", ".xml", ".sh", ".py", ".json"},
                       ["android", "deploy", "scripts", "tools"]):
            try:
                text = p.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            for i, ln in enumerate(text.splitlines(), 1):
                if is_comment(ln):
                    continue
                for m in PAT_CRED.finditer(ln):
                    ident, val = m.group(1), m.group(2)
                    # 排除樣板／佔位符／資源參考
                    if val.startswith(("@string", "${", "%", "<")) or val in ("", "null", "true", "false"):
                        continue
                    if PAT_CRED_ALWAYS.search(ident):
                        pass
                    elif PAT_CRED_KEYNAME.search(ident) and PAT_KEYLIKE.search(val):
                        pass
                    else:
                        continue
                    hits.append((p, i, ident, val))
        if not hits:
            print("（無）")
        for p, i, ident, val in hits:
            shown = val if len(val) <= 24 else val[:21] + "…"
            print(f"  {str(p.relative_to(ROOT)):58s}:{i:<4} {ident} = \"{shown}\"")
        print(f"\n合計 {len(hits)} 筆。處置原則：預設值可留（如空字串），"
              "**可用的固定憑證**必須改為每次安裝／每次啟動隨機產生或由使用者設定。")

    # 8) native 權限
    section("native 端檔案權限：world-accessible chmod", 8, only)
    if only in (None, 8):
        hits = []
        for p in files({".c", ".cc", ".cpp", ".h"}, ["deploy", "source_code", "tools", "android"]):
            for i, ln in enumerate(p.read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
                if is_comment(ln):
                    continue
                for m in PAT_CHMOD.finditer(ln):
                    mode = int(m.group(1), 8)
                    if mode & 0o007:
                        hits.append((p, i, m.group(1)))
        if not hits:
            print("（無）")
        for p, i, mode in hits:
            print(f"  {str(p.relative_to(ROOT)):58s}:{i:<4} chmod(..., {mode})  ← 其他 uid 可存取")
        print(f"\n合計 {len(hits)} 筆。IPC socket 若由 root 建立而被 untrusted_app 使用，"
              "需要的是**存取控制設計**（專用 group／SELinux type／權杖），不是放寬成 0777。")


if __name__ == "__main__":
    main()
