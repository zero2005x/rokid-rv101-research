#!/usr/bin/env python3
"""Check 1.19.009 firmware for F-01~F-08 vulnerability items from inventory report."""
import re, os
from datetime import date

BASE = r"c:\Users\<USER>\Documents\codebase\rokid_root\output_1.19"

def search_img(fname, patterns):
    path = os.path.join(BASE, fname)
    data = open(path, "rb").read()
    results = {}
    for key, pat in patterns.items():
        matches = re.findall(pat, data, re.IGNORECASE)
        results[key] = [re.sub(b"[^\x20-\x7e]", b" ", m).decode("ascii", "replace").strip()
                        for m in matches if len(m) > 3]
    return results

print("=" * 65)
print("  1.19.009-20260520 Firmware Vulnerability Verification")
print("=" * 65)

# --- system.img checks ---
sys_r = search_img("system.img", {
    "security_patch": rb"ro\.build\.version\.security_patch=[^\n\x00]{4,20}",
    "fingerprint":    rb"ro\.build\.fingerprint=[^\n\x00]{10,80}",
    "usb_config":     rb"persist\.sys\.usb\.config=[^\n\x00]{1,30}",
    "debuggable":     rb"ro\.debuggable=[^\n\x00]{1,5}",
    "secure":         rb"ro\.secure=[^\n\x00]{1,5}",
})
print("\n[system.img]")
for k, v in sys_r.items():
    if v:
        print(f"  {k}: {v[0]}")

# --- vendor.img checks ---
vendor_r = search_img("vendor.img", {
    "vendor_security_patch": rb"ro\.vendor\.build\.security_patch=[^\n\x00]{4,20}",
    "vendor_usb_config":     rb"persist\.sys\.usb\.config=[^\n\x00]{1,30}",
    "adb_diag":              rb"adb,diag",
    "tcp_8341":              rb"[^0-9]8341[^0-9]",
})
print("\n[vendor.img]")
for k, v in vendor_r.items():
    if v:
        print(f"  {k}: {v[0]}")
    else:
        print(f"  {k}: NOT FOUND")

# --- vendor_dlkm.img checks ---
dlkm_r = search_img("vendor_dlkm.img", {
    "fastrpc_vermagic": rb"name=frpc_adsprpc\x00.{0,300}vermagic=[^\x00]{10,80}",
    "kgsl_vermagic":    rb"name=msm_kgsl\x00.{0,300}vermagic=[^\x00]{10,80}",
    "fastrpc_scmver":   rb"name=frpc_adsprpc\x00.{0,600}scmversion=[^\x00]{8,20}",
    "kgsl_scmver":      rb"name=msm_kgsl\x00.{0,600}scmversion=[^\x00]{8,20}",
})
print("\n[vendor_dlkm.img - kernel modules]")
for k, v in dlkm_r.items():
    if v:
        # clean up junk in between
        clean = re.sub(r"\s{2,}", " ", v[0])[:100]
        print(f"  {k}: {clean}")

# --- Summary ---
print("\n" + "=" * 65)
print("  CVE Status vs Inventory Report (1.18.007 baseline)")
print("=" * 65)

sp = sys_r.get("security_patch", ["UNKNOWN"])[0]
sp_date = sp.split("=")[-1] if "=" in sp else "UNKNOWN"

def before_asb(value, year, month, day):
    try:
        parsed = date.fromisoformat(value[:10])
    except ValueError:
        return True
    return parsed < date(year, month, day)

rows = [
    ("F-01/02", "CVE-2024-43047 FastRPC adsprpc UAF",
     "Android ASB 2024-11-05", "Patch > 2024-07-05 baseline",
     "UNPATCHED" if before_asb(sp_date, 2024, 11, 5) else "PATCHED BY BASELINE"),
    ("F-03/04", "CVE-2025-21479/21480 Adreno kgsl",
     "Android ASB 2025-08-05", "Patch >> baseline",
     "UNPATCHED" if sp_date < "2025-08" else "PATCHED"),
    ("F-05",    "CVE-2025-27038 kgsl+WebView",
     "Android ASB 2025-08-05", "Patch >> baseline",
     "UNPATCHED" if sp_date < "2025-08" else "PATCHED"),
    ("F-08",    "Security patch baseline frozen",
     "ro.build.version.security_patch", sp_date,
     "CONFIRMED FROZEN" if sp_date == "2024-07-05" else f"UPDATED → {sp_date}"),
]

adb_diag_found = bool(vendor_r.get("adb_diag"))
rows += [
    ("F-06", "USB DIAG (adb,diag) persistent",
     "persist.sys.usb.config",
     "adb,diag present" if adb_diag_found else "NOT FOUND in firmware",
     "REMOVED from firmware" if not adb_diag_found else "STILL PRESENT"),
    ("F-07", "TCP 8341 root service",
     "Port binding",
     "8341 in vendor/system imgs" if vendor_r.get("tcp_8341") else "NOT FOUND in firmware",
     "NOT IN FIRMWARE (may be runtime)" if not vendor_r.get("tcp_8341") else "PRESENT"),
]

for item_id, name, basis, evidence, status in rows:
    icon = "✗" if "UNPATCHED" in status or "FROZEN" in status or "PRESENT" in status else "✓"
    print(f"\n  {icon} [{item_id}] {name}")
    print(f"       Basis   : {basis}")
    print(f"       Evidence: {evidence}")
    print(f"       Status  : {status}")

print(f"\n  Security Patch in 1.19.009: {sp_date}")
print(f"  Previous baseline (1.18.007): 2024-07-05")
if sp_date == "2024-07-05":
    print("  !! Patch date UNCHANGED — all kernel CVEs remain unaddressed !!")
else:
    print(f"  >> Patch date advanced to {sp_date}")
print()
