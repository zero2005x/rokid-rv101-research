#!/usr/bin/env python3
# gen_redacted_pdf.py — render docs/ghostlock-before-after-redacted.md into the PDF to send to Rokid.
#
# Why this exists: the original PDF was printed from Chrome on Windows on 2026-09-06, so it
# drifts out of sync with the .md (there was a case where the .md had been updated but the PDF
# still said `[待補]`). Always rerun this script after editing the .md.
#
# Layout notes (pitfalls already hit):
#   - Table `table-layout: fixed` + `overflow-wrap: anywhere`: otherwise the SHA-256 / OTA file
#     names push the right column off the page and it gets clipped.
#   - §1 is split into one table per version: putting both versions side by side forces the
#     64-character hashes to wrap, which makes copy-and-compare error-prone.
#
# Usage: python3 scripts/gen_redacted_pdf.py
# Depends on: weasyprint (`pip install weasyprint`); CJK needs Droid Sans Fallback or Noto CJK
# installed on the system.
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
