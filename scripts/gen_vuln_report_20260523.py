#!/usr/bin/env python3
"""
Generate ROKID_Glasses_漏洞盤點_20260523.xlsx
Updated from the 20260522 version with results measured on the 1.19.009-20260520 firmware
"""
import openpyxl
from openpyxl.styles import (
    PatternFill, Font, Alignment, Border, Side, GradientFill
)
from openpyxl.utils import get_column_letter

wb = openpyxl.Workbook()

# ── colour definitions ────────────────────────────────────────
RED_FILL      = PatternFill("solid", fgColor="C00000")
ORANGE_FILL   = PatternFill("solid", fgColor="E26B0A")
YELLOW_FILL   = PatternFill("solid", fgColor="FFCC00")
GREEN_FILL    = PatternFill("solid", fgColor="375623")
GRAY_FILL     = PatternFill("solid", fgColor="808080")
BLUE_HDR      = PatternFill("solid", fgColor="1F497D")
SECTION_FILL  = PatternFill("solid", fgColor="17375E")
ROW_ALT       = PatternFill("solid", fgColor="DCE6F1")
WHITE_FILL    = PatternFill("solid", fgColor="FFFFFF")
LIGHT_GRAY    = PatternFill("solid", fgColor="F2F2F2")
AMBER_FILL    = PatternFill("solid", fgColor="FFC000")

WHITE  = Font(color="FFFFFF", bold=True)
BLACK  = Font(color="000000")
BLACK_B= Font(color="000000", bold=True)
RED_F  = Font(color="C00000", bold=True)
GREEN_F= Font(color="375623", bold=True)

thin = Side(style="thin", color="AAAAAA")
med  = Side(style="medium", color="666666")
BORDER_THIN = Border(left=thin, right=thin, top=thin, bottom=thin)
BORDER_MED  = Border(left=med, right=med, top=med, bottom=med)

def hdr_cell(ws, row, col, val, fill=BLUE_HDR, font=WHITE, wrap=True, bold=True):
    c = ws.cell(row=row, column=col, value=val)
    c.fill = fill
    c.font = Font(color=font.color, bold=bold, size=10)
    c.alignment = Alignment(horizontal="center", vertical="center",
                             wrap_text=wrap)
    c.border = BORDER_THIN
    return c

def data_cell(ws, row, col, val, fill=None, font=None, align="left",
              wrap=True, bold=False):
    c = ws.cell(row=row, column=col, value=val)
    if fill:
        c.fill = fill
    c.font = Font(color=(font.color if font else "000000"),
                  bold=bold, size=10)
    c.alignment = Alignment(horizontal=align, vertical="center",
                             wrap_text=wrap)
    c.border = BORDER_THIN
    return c

# ═══════════════════════════════════════════════════════════════
# Sheet 1: summary
# ═══════════════════════════════════════════════════════════════
ws1 = wb.active
ws1.title = "摘要"
ws1.column_dimensions["A"].width = 26
ws1.column_dimensions["B"].width = 90

# header row
ws1.row_dimensions[1].height = 28
ws1.merge_cells("A1:B1")
c = ws1.cell(row=1, column=1, value="Rokid Glasses 安全盤點 — 綜合摘要")
c.fill = SECTION_FILL
c.font = Font(color="FFFFFF", bold=True, size=14)
c.alignment = Alignment(horizontal="center", vertical="center")

# subtitle
ws1.merge_cells("A2:B2")
c2 = ws1.cell(row=2, column=1,
              value="三階段 ADB 盤查整併 + 1.19.009 韌體靜態驗證 / "
                    "產生日 2026-05-23 / 盤點性質：唯讀稽核，不含 PoC 或利用步驟")
c2.fill = PatternFill("solid", fgColor="4472C4")
c2.font = Font(color="FFFFFF", size=9)
c2.alignment = Alignment(horizontal="center", vertical="center")

# basic-information header
ws1.row_dimensions[4].height = 18
hdr_cell(ws1, 4, 1, "裝置基本資料", fill=SECTION_FILL, bold=True)
ws1.cell(row=4, column=2).fill = SECTION_FILL

device_info = [
    ("裝置",         "Rokid Glasses (RG-glasses / RV101)"),
    ("OS / 架構",    "Android 12  •  arm64-v8a"),
    ("平台 / 晶片",  "Qualcomm NEO LUNA-V2 (SSG2_AURORA)  •  4× Cortex-A55  •  Adreno (max 644 MHz)"),
    ("建置指紋 (1.19.009)",
                     "Rokid/glasses/glasses:12/SKQ1.240613.001/1.19.009-20260520-150201:user/release-keys"),
    ("韌體建置日",   "2026-05-20  ← 更新自 1.18.007-20260427"),
    ("安全補丁基線", "2024-07-05  (build 與 vendor 一致) — 凍結，約 23 個月落差  ⚠️ 1.19.009 未更新"),
    ("Kernel",       "5.10.209-perf-g2b7ae077b782-ab1.19.009-20260520-150201  "
                     "(git commit 更新，版本號不變)"),
    ("SELinux",      "Enforcing"),
    ("除錯姿態",     "ro.debuggable=0  •  ro.secure=1  (基線健康)"),
    ("USB 組態",     "persist.sys.usb.config = none  ✅ 1.19.009 固件預設已移除 adb,diag"),
    ("對外監聽 8341","vendor/system 固件層未找到 8341 綁定字串  ⚠️ 需刷機後 adb shell ss -tlnp 確認"),
]

for i, (k, v) in enumerate(device_info):
    row = 5 + i
    fill = ROW_ALT if i % 2 == 0 else WHITE_FILL
    c_k = ws1.cell(row=row, column=1, value=k)
    c_k.fill = PatternFill("solid", fgColor="BDD7EE")
    c_k.font = Font(bold=True, size=10)
    c_k.alignment = Alignment(horizontal="left", vertical="center")
    c_k.border = BORDER_THIN
    c_v = ws1.cell(row=row, column=2, value=v)
    c_v.fill = fill
    c_v.font = Font(size=10)
    c_v.alignment = Alignment(horizontal="left", vertical="center", wrap_text=True)
    c_v.border = BORDER_THIN
    ws1.row_dimensions[row].height = 20

# key conclusions
ws1.row_dimensions[16].height = 8
ws1.row_dimensions[17].height = 18
hdr_cell(ws1, 17, 1, "核心結論", fill=SECTION_FILL, bold=True)
ws1.cell(row=17, column=2).fill = SECTION_FILL

conclusions = [
    "根因：安全補丁凍結於 2024-07-05。1.19.009 (2026-05-20 發布) 未更新補丁基線，"
    "任何於 2024-08 之後修補的 CVE 仍視為未修。",
    "確定未修且在 CISA KEV：FastRPC 叢集 (CVE-2024-43047 等) 與 2025 Adreno 叢集 "
    "(CVE-2025-21479/21480/27038)。",
    "改善項目：① USB DIAG — 1.19.009 固件預設 persist.sys.usb.config=none，"
    "字串 adb,diag 已從 system/vendor 映像消失；"
    "② TCP 8341 — vendor/system 映像層均無 8341 綁定，需開機確認是否真正關閉。",
    "本機 DAC/MAC/除錯姿態健康，故可行的本地提權路徑是核心層記憶體破壞 CVE（繞過 SELinux），而非設定鬆動。",
    "2023 GPU/DSP 叢集 (33106/33107/33063/22071) 修補於 2023-12，早於 2024-07 基線，研判已含入 → 待覆核。",
    "唯一根治：促 OEM (Rokid) 推送含 2025-08 以後 ASB 的韌體；刷機後補做 TCP 8341 運行時確認。",
]
for i, txt in enumerate(conclusions):
    row = 18 + i
    c_bullet = ws1.cell(row=row, column=1, value="•")
    c_bullet.fill = PatternFill("solid", fgColor="E9EFF7")
    c_bullet.font = Font(bold=True, size=12, color="1F497D")
    c_bullet.alignment = Alignment(horizontal="center", vertical="center")
    c_bullet.border = BORDER_THIN
    c_txt = ws1.cell(row=row, column=2, value=txt)
    c_txt.fill = PatternFill("solid", fgColor="EEF3FB")
    c_txt.font = Font(size=10)
    c_txt.alignment = Alignment(horizontal="left", vertical="center", wrap_text=True)
    c_txt.border = BORDER_THIN
    ws1.row_dimensions[row].height = 36

# ═══════════════════════════════════════════════════════════════
# Sheet 2: findings list
# ═══════════════════════════════════════════════════════════════
ws2 = wb.create_sheet("發現清單")
ws2.column_dimensions["A"].width = 7
ws2.column_dimensions["B"].width = 12
ws2.column_dimensions["C"].width = 38
ws2.column_dimensions["D"].width = 40
ws2.column_dimensions["E"].width = 8
ws2.column_dimensions["F"].width = 9
ws2.column_dimensions["G"].width = 35
ws2.column_dimensions["H"].width = 20
ws2.column_dimensions["I"].width = 8
ws2.column_dimensions["J"].width = 45

headers = ["ID", "類別", "項目 / CVE", "對應元件 / 證據",
           "CVSS", "在 KEV?", "補丁對照 (vs 2024-07-05)",
           "狀態", "優先序", "建議"]
ws2.row_dimensions[1].height = 22
for col, h in enumerate(headers, start=1):
    hdr_cell(ws2, 1, col, h)

# data row definitions
# (id, category, item/CVE, component evidence, cvss, kev, patch comparison, status,
#  priority, recommendation, status colour)
UNPATCHED  = "已驗證未修"
IMPROVED   = "已改善(固件層)"
PENDING_RT = "固件層未見，待運行確認"
CLEARED    = "已釐清"
PENDING_RV = "待覆核"
PENDING_GAP= "資訊缺口"

findings = [
    ("F-01", "核心 CVE",
     "CVE-2024-43047\n(FastRPC/adsprpc UAF)",
     "/dev/adsprpc-smd (crw-rw-r--, vendor_qdsp_device);\n"
     "adsprpcd/cdsprpcd 在跑;\n"
     "frpc_adsprpc scmversion=g2b7ae077b782 (重新編譯非補丁)",
     "7.8", "是",
     "Android 修補 2024-11-05 > 基線 → 未修\n"
     "1.19.009 安全補丁仍 2024-07-05，module 僅重新編譯",
     UNPATCHED, "P0",
     "促 OEM 升級至含 2024-11 以後 ASB；過渡期監控 DSP 異常",
     "RED"),

    ("F-02", "核心 CVE (同 F-01 叢集)",
     "CVE-2024-38402/21455/33060/49848\n(adsprpc 叢集，含 NoviSpy 利用鏈)",
     "同 F-01 節點；皆 CISA KEV 已收錄",
     "高(見公告)", "是(叢集)",
     "皆 2024 下半修補 > 基線 → 未修\n"
     "1.19.009 無新補丁",
     UNPATCHED, "P0",
     "與 F-01 同批修補",
     "RED"),

    ("F-03", "核心 CVE",
     "CVE-2025-21479\n(Adreno kgsl 授權不當)",
     "/dev/kgsl-3d0 (app 可達);\n"
     "msm_kgsl scmversion=g2b7ae077b782 (重新編譯非補丁)",
     "8.6", "否",
     "Android 修補 2025-08-05 ≫ 基線 → 未修\n"
     "1.19.009 安全補丁仍 2024-07-05",
     UNPATCHED, "P0",
     "促 OEM 升級至含 2025-08 以後 ASB",
     "RED"),

    ("F-04", "核心 CVE",
     "CVE-2025-21480\n(Adreno kgsl 授權不當)",
     "/dev/kgsl-3d0;\nsame as F-03",
     "8.6", "否",
     "同 F-03 — Android 修補 2025-08-05 → 未修",
     UNPATCHED, "P0",
     "同 F-03",
     "RED"),

    ("F-05", "核心 CVE",
     "CVE-2025-27038\n(Adreno UAF + Chrome/WebView 渲染)",
     "/dev/kgsl-3d0 + WebView/Chromium",
     "7.5", "否",
     "Android 修補 2025-08-05 → 未修\n"
     "1.19.009 安全補丁仍 2024-07-05",
     UNPATCHED, "P1",
     "同 F-03；並更新 WebView/Chromium 版本",
     "RED"),

    ("F-06", "設定暴露",
     "USB DIAG 持久開啟",
     "1.18.007: sys.usb.config = adb,diag\n"
     "1.19.009: persist.sys.usb.config = none\n"
     "vendor/system 映像已無 adb,diag 字串",
     "N/A", "—",
     "設定問題，非 ASB 範疇\n1.19.009 固件預設已移除",
     IMPROVED, "P1→P2",
     "刷機後 getprop persist.sys.usb.config 確認是否維持 none；"
     "限制實體 USB 接觸",
     "GREEN"),

    ("F-07", "對外 root 服務",
     "TCP 0.0.0.0:8341 LISTEN (uid=0)\n(非 HTTP 自訂協定)",
     "1.18.007: uid=0 監聽; curl 連線成功但無 HTTP 回應\n"
     "1.19.009: vendor/system 映像均無 8341 綁定字串\n"
     "product.img 未驗證; 可能動態載入",
     "N/A", "—",
     "服務/設定問題; 固件層靜態搜尋未見",
     PENDING_RT, "P1(待確認)",
     "刷機後 adb shell ss -tlnp | grep 8341 確認;\n"
     "若仍出現：定性協定後綁回 127.0.0.1 或關閉",
     "AMBER"),

    ("F-08", "根因",
     "安全補丁基線凍結 (2024-07-05)\n"
     "1.19.009 確認未更新",
     "ro.build.version.security_patch=2024-07-05 (system.img)\n"
     "ro.vendor.build.security_patch=2024-07-05 (vendor.img)\n"
     "1.19.009 同樣凍結，落差擴大至 ≈23 個月",
     "—", "—",
     "≈23 個月 ASB 落差 (截至 2026-05-23)",
     "已驗證(仍凍結)", "P0(系統性)",
     "唯一根治途徑：OEM 推送新韌體 (ASB ≥ 2025-08);\n"
     "無新韌體前風險無法於裝置端消除",
     "RED"),

    ("F-09", "待覆核",
     "CVE-2023-33106/33107/33063\nCVE-2022-22071\n(2023 GPU/DSP 叢集)",
     "kgsl / dsp 路徑;\n修補 2023-12 < 基線 2024-07-05 → 研判已含入",
     "高/8.4", "否",
     "修補 2023-12 < 基線 → 研判已含入\n需 QSSI merge 確認",
     PENDING_RV, "P3",
     "比對 vendor 映像確認 QSSI 實際 merge，再結案",
     "GRAY"),

    ("F-10", "資訊性",
     "/dev/kgsl-3d0 為 world-rw",
     "crw-rw-rw-, context u:object_r:gpu_device:s0",
     "—", "—",
     "Android 標準 (app 需 GPU)；非設定錯誤",
     CLEARED, "低",
     "無需處置；風險來自背後未修驅動 (見 F-03~05)",
     "GRAY"),

    ("F-11", "資訊性",
     "DMA_HEAP 節點 world-readable",
     "全為 cr--r--r-- (唯讀)，secure heap context 各自隔離",
     "—", "—",
     "唯讀 + context 隔離",
     CLEARED, "低",
     "無需處置",
     "GRAY"),

    ("F-12", "資訊性",
     "JDWP / 除錯防線",
     "JDWP 僅 2 條；ro.debuggable=0；getenforce=Enforcing",
     "—", "—",
     "姿態健康",
     CLEARED, "低",
     "無需處置（先前疑慮排除）",
     "GRAY"),

    ("F-13", "待補",
     "Keymaster / TEE 整合版本未取得",
     "getprop grep 無對應名稱；KeyMint HAL 與 keymint-service-qti 在跑",
     "—", "—",
     "資訊缺口",
     PENDING_GAP, "—",
     "改以 dumpsys android.hardware.security.keymint.IKeyMintDevice/default;\n"
     "或讀 vendor 屬性檔",
     "GRAY"),
]

STATUS_FILLS = {
    "RED":   PatternFill("solid", fgColor="FFCCCC"),
    "GREEN": PatternFill("solid", fgColor="C6EFCE"),
    "AMBER": PatternFill("solid", fgColor="FFEB9C"),
    "GRAY":  PatternFill("solid", fgColor="EEEEEE"),
}
STATUS_FONTS = {
    "RED":   Font(color="9C0006", bold=True, size=10),
    "GREEN": Font(color="276221", bold=True, size=10),
    "AMBER": Font(color="7E5B00", bold=True, size=10),
    "GRAY":  Font(color="444444", bold=True, size=10),
}

for i, row_data in enumerate(findings):
    row = i + 2
    ws2.row_dimensions[row].height = 54
    fid, cat, item, evidence, cvss, kev, patch, status, prio, rec, color = row_data
    row_fill = ROW_ALT if i % 2 == 0 else WHITE_FILL
    vals = [fid, cat, item, evidence, cvss, kev, patch, status, prio, rec]
    for col, val in enumerate(vals, start=1):
        if col == 8:  # status column
            c = data_cell(ws2, row, col, val,
                          fill=STATUS_FILLS[color],
                          font=STATUS_FONTS[color],
                          align="center")
        elif col in (5, 6, 9):
            c = data_cell(ws2, row, col, val, fill=row_fill, align="center")
        else:
            c = data_cell(ws2, row, col, val, fill=row_fill)

# ═══════════════════════════════════════════════════════════════
# Sheet 3: verification and follow-up
# ═══════════════════════════════════════════════════════════════
ws3 = wb.create_sheet("驗證與後續")
ws3.column_dimensions["A"].width = 10
ws3.column_dimensions["B"].width = 32
ws3.column_dimensions["C"].width = 85

ws3.merge_cells("A1:C1")
c = ws3.cell(row=1, column=1,
             value="驗證命令（唯讀）與未收斂項目後續  |  以下皆為盤點/驗證用途；不含利用或 PoC")
c.fill = SECTION_FILL
c.font = Font(color="FFFFFF", bold=True, size=12)
c.alignment = Alignment(horizontal="center", vertical="center")
ws3.row_dimensions[1].height = 22

for col, h in enumerate(["關聯 ID", "目的", "命令 / 作法"], start=1):
    hdr_cell(ws3, 2, col, h)
ws3.row_dimensions[2].height = 20

actions = [
    ("F-08",
     "確認補丁基線 (已完成 1.19.009 靜態驗證)",
     "# 靜態驗證 (已完成)\n"
     "strings output_1.19/system.img | grep security_patch\n"
     "strings output_1.19/vendor.img | grep security_patch\n"
     "# → 均為 2024-07-05，與 1.18.007 相同\n\n"
     "# 開機確認\n"
     "adb shell getprop ro.build.version.security_patch"),

    ("F-01/02",
     "FastRPC 節點與行程確認",
     "adb shell 'ls -lZ /dev/adsprpc-smd*'\n"
     "adb shell 'ps -A | grep -E \"adsprpcd|cdsprpcd|sscrpcd\"'"),

    ("F-03/04/05",
     "GPU 節點與 kgsl 版本",
     "adb shell 'ls -lZ /dev/kgsl-3d0'\n"
     "adb shell 'cat /sys/class/kgsl/kgsl-3d0/device/version 2>/dev/null'"),

    ("F-06",
     "USB DIAG 組態確認 (刷 1.19.009 後)",
     "adb shell getprop sys.usb.config\n"
     "adb shell getprop persist.sys.usb.config\n"
     "adb shell 'ls -lZ /dev/diag 2>/dev/null'\n"
     "# 預期: none (固件預設已改善)"),

    ("F-07",
     "TCP 8341 運行時確認 (刷 1.19.009 後必做)",
     "# 確認是否仍監聽\n"
     "adb shell 'ss -tlnp | grep 8341'\n"
     "adb shell 'cat /proc/net/tcp | grep 20AD'  # 8341=0x20AD\n\n"
     "# 若仍存在，補做原始 socket Read 定性協定:\n"
     "# PowerShell (轉 adb forward 後):\n"
     "$c=New-Object Net.Sockets.TcpClient('127.0.0.1',8341)\n"
     "$s=$c.GetStream();$s.ReadTimeout=3000\n"
     "$b=New-Object byte[] 4096\n"
     "try{$n=$s.Read($b,0,4096);($b[0..($n-1)]|%{$_.ToString('X2')}) -join ' '}catch{'no banner'}\n"
     "$g=[Text.Encoding]::ASCII.GetBytes(\"GET / HTTP/1.0`r`n`r`n\")\n"
     "$s.Write($g,0,$g.Length);Start-Sleep -m 500\n"
     "try{$n=$s.Read($b,0,4096);[Text.Encoding]::ASCII.GetString($b,0,$n)}catch{'非HTTP'}\n"
     "$c.Close()"),

    ("F-08",
     "1.19.009 韌體靜態驗證摘要 (已完成)",
     "# 已驗證事項 (2026-05-23):\n"
     "# boot.img   kernel: 5.10.209-perf-g2b7ae077b782-ab1.19.009-20260520-150201\n"
     "# system.img ro.build.version.security_patch=2024-07-05 ← UNCHANGED\n"
     "# vendor.img ro.vendor.build.security_patch=2024-07-05  ← UNCHANGED\n"
     "# vendor.img persist.sys.usb.config=none               ← IMPROVED\n"
     "# vendor_dlkm frpc_adsprpc scmversion=g2b7ae077b782    (重編非補丁)\n"
     "# vendor_dlkm msm_kgsl    scmversion=g2b7ae077b782    (重編非補丁)\n"
     "# vendor/system: TCP 8341 字串未發現                  ← PENDING runtime"),

    ("F-09",
     "覆核 2023 叢集是否實際 merge",
     "比對 vendor 映像版本 / QSSI 合併紀錄；確認後將 F-09 結案\n"
     "adb shell 'cat /vendor/build.prop | grep -i qssi'\n"
     "adb shell 'getprop ro.vendor.qssi.version 2>/dev/null'"),

    ("F-13",
     "補抓 TEE/Keymaster 版本",
     "adb shell 'dumpsys android.hardware.security.keymint.IKeyMintDevice/default 2>/dev/null'\n"
     "adb shell 'getprop | grep -E \"keymaster|keymint|tee\"'\n"
     "# 若 dumpsys 無輸出，讀 vendor 屬性檔:\n"
     "adb shell 'cat /vendor/etc/selinux/vendor_property_contexts | grep key'"),
]

for i, (aid, purpose, cmd) in enumerate(actions):
    row = i + 3
    ws3.row_dimensions[row].height = 72
    row_fill = ROW_ALT if i % 2 == 0 else WHITE_FILL
    data_cell(ws3, row, 1, aid, fill=row_fill, align="center", bold=True)
    data_cell(ws3, row, 2, purpose, fill=row_fill)
    c = ws3.cell(row=row, column=3, value=cmd)
    c.fill = PatternFill("solid", fgColor="F8F8F8")
    c.font = Font(name="Consolas", size=9, color="003366")
    c.alignment = Alignment(horizontal="left", vertical="top", wrap_text=True)
    c.border = BORDER_THIN

# notes row
note_row = len(actions) + 3
ws3.row_dimensions[note_row].height = 20
ws3.merge_cells(f"A{note_row}:C{note_row}")
c = ws3.cell(row=note_row, column=1,
             value="說明：本表為防禦性資產盤點與風險分流之用。CVSS/KEV 請以 CISA KEV 目錄與 "
                   "Qualcomm 月度公告即時核對；修補對照以官方 Android Security Bulletin 日期為準。"
                   "  韌體驗證基於 1.19.009-20260520-150201 (MD5: 8dc5a3d0a953e682ee4a4ccc9149dfb1)。")
c.fill = PatternFill("solid", fgColor="E2EFDA")
c.font = Font(size=9, italic=True, color="444444")
c.alignment = Alignment(horizontal="left", vertical="center", wrap_text=True)
c.border = BORDER_THIN

# ── save ──────────────────────────────────────────────────────
out_path = r"c:\Users\<USER>\Documents\codebase\rokid_root\ROKID_Glasses_漏洞盤點_20260523.xlsx"
wb.save(out_path)
print(f"[OK] 已儲存: {out_path}")
