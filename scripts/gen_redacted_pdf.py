#!/usr/bin/env python3
# gen_redacted_pdf.py — 把 docs/ghostlock-before-after-redacted.md 產生成要寄給 Rokid 的 PDF。
#
# 為什麼需要這支：原 PDF 是 2026-09-06 在 Windows 用 Chrome 列印的，內容與 .md 會不同步
# （曾發生 .md 已更新、PDF 仍留 `[待補]` 的情形）。改 .md 後請一律重跑本腳本。
#
# 排版要點（踩過的坑）：
#   - 表格 `table-layout: fixed` ＋ `overflow-wrap: anywhere`：否則 SHA-256 / OTA 檔名會把右欄推出頁面被裁掉。
#   - §1 拆成「每版一張表」：兩版並排會讓 64 字元雜湊被迫折行，複製比對容易出錯。
#
# 用法：python3 scripts/gen_redacted_pdf.py
# 依賴：weasyprint（`pip install weasyprint`）；CJK 需系統有 Droid Sans Fallback 或 Noto CJK。
import subprocess, markdown, sys, os
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src=os.path.join(ROOT,'docs/ghostlock-before-after-redacted.md')
out=os.path.join(ROOT,'docs/ghostlock-before-after-redacted.pdf')
html = markdown.markdown(open(src,encoding='utf-8').read(), extensions=['tables','fenced_code'])
doc = f"""<!DOCTYPE html><html><head><meta charset="utf-8"><style>
@page {{ size: A4; margin: 16mm 14mm; }}
body {{ font-family: "Droid Sans Fallback","Noto Sans CJK TC",sans-serif; font-size: 10pt; line-height: 1.45; }}
h1 {{ font-size: 15pt; }} h2 {{ font-size: 12.5pt; margin-top: 13pt; }} h3 {{ font-size: 11pt; }}
table {{ border-collapse: collapse; width: 100%; table-layout: fixed; margin: 8pt 0; }}
th,td {{ border: 1px solid #888; padding: 3pt 5pt; vertical-align: top; font-size: 8.5pt;
        overflow-wrap: anywhere; word-break: break-word; }}
th {{ background: #eee; }}
td:first-child, th:first-child {{ width: 20%; }}
code,pre {{ font-family: "DejaVu Sans Mono",monospace; font-size: 8.5pt; background: #f5f5f5;
           overflow-wrap: anywhere; word-break: break-all; }}
pre {{ padding: 6pt; border: 1px solid #ddd; white-space: pre-wrap; }}
blockquote {{ margin: 6pt 0; padding-left: 8pt; border-left: 3px solid #bbb; color: #333; font-size: 9.5pt; }}
</style></head><body>{html}</body></html>"""
htmlpath=os.path.join(ROOT,'build-cache/redacted_preview.html')
os.makedirs(os.path.dirname(htmlpath),exist_ok=True)
open(htmlpath,'w',encoding='utf-8').write(doc)
r=subprocess.run(['weasyprint',htmlpath,out],capture_output=True,text=True)
print("weasyprint rc=",r.returncode, r.stderr.strip()[:200])
print("產出:",out)
