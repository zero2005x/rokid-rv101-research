#!/usr/bin/env python3
"""
ghostlock_patch_check.py — 自動檢查 Rokid OTA 是否修補 GhostLock (CVE-2026-43499)

流程（一條龍）：
  1. 查最新版本（已破解簽名）→ 取得 imageUrl / version / checksum
  2. 下載 OTA
  3. payload.bin → boot.img（payload-dumper-go）
  4. boot.img → kernel Image（解析 Android boot image header）
  5. kernel Image → 符號（vmlinux-to-elf）→ 找 remove_waiter
  6. 反組譯 remove_waiter（capstone）
  7. 與官方 patch（commit 3bfdc63）比對：判斷用 `current`（sp_el0，漏洞版）
     還是 `waiter->task`（修補版）
  8. 產生固定格式報告

依賴：
  - Python3 + capstone + vmlinux-to-elf（pip install capstone vmlinux-to-elf）
  - payload-dumper-go（https://github.com/ssut/payload-dumper-go）
  - curl / unzip / nm（binutils）
  - adb（讀裝置目前版本與簽名參數，可省略）

用法：
  python3 ghostlock_patch_check.py                 # 全自動
  python3 ghostlock_patch_check.py --ota <zip>     # 對已下載的 OTA zip 分析
  python3 ghostlock_patch_check.py --boot <boot.img> # 對已抽出的 boot.img 分析
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request
import urllib.error

# ============================ 參數 ============================================
KEY = os.environ.get("KEY", "<REDACTED_OTA_KEY>")
SECRET = os.environ.get("SECRET", "<REDACTED_OTA_SECRET>")
DTID = os.environ.get("DTID", "<REDACTED_OTA_DEVICETYPEID>")
DID = os.environ.get("DID", "<DEVICE_SERIAL>")
SERVICE = os.environ.get("SERVICE", "ota")
SVER = os.environ.get("SVER", "1.0")
CHECK_URL = os.environ.get("CHECK_URL", "https://ota.rokid.com")
CHECK_API = os.environ.get("CHECK_API", "/v1/extended/ota/check")
GHOSTLOCK_SPL = os.environ.get("GHOSTLOCK_SPL", "2026-07-05")

ADB = os.environ.get("ADB", "/mnt/c/Users/<USER>/AppData/Local/Android/Sdk/platform-tools/adb.exe")
SERIAL = os.environ.get("SERIAL", "<DEVICE_SERIAL>")

PDGO = os.environ.get("PDGO", "payload-dumper-go")          # payload-dumper-go 路徑
VMLINUX_TO_ELF = os.environ.get("VMLINUX_TO_ELF", "vmlinux-to-elf")
NM = os.environ.get("NM", "nm")

# remove_waiter 在 rt_mutex_waiter 的 task 欄位偏移（2 個 rb_node = 2*24 bytes）
WAITER_TASK_OFFSET = 0x30

# ============================ 工具函式 ========================================
def sh(cmd, **kw):
    """執行 subprocess，回傳 (returncode, stdout, stderr)。"""
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True, **kw)
    return p.returncode, p.stdout, p.stderr


def adb_getprop(prop):
    if not ADB:
        return ""
    rc, out, _ = sh(f'"{ADB}" -s {SERIAL} shell getprop {prop} 2>/dev/null')
    return out.strip() if rc == 0 else ""


def make_sign(ts):
    s = (f"key={KEY}&device_type_id={DTID}&device_id={DID}"
         f"&service={SERVICE}&version={SVER}&time={ts}&secret={SECRET}")
    return hashlib.md5(s.encode()).hexdigest()


def check_latest(system_version):
    """查詢最新版本，回傳 dict（含 imageUrl/version/checksum）。"""
    ts = int(time.time())
    sign = make_sign(ts)
    auth = (f"version={SVER};time={ts};sign={sign};key={KEY};"
            f"device_type_id={DTID};device_id={DID};service={SERVICE}")
    body = json.dumps({"version": system_version, "osType": "", "cpuType": ""})
    req = urllib.request.Request(CHECK_URL + CHECK_API, data=body.encode(), method="POST")
    req.add_header("Content-type", "application/json;charset=utf-8")
    req.add_header("Authorization", auth)
    try:
        resp = urllib.request.urlopen(req, timeout=20)
        return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        print(f"[x] HTTP {e.code}: {e.read().decode()}")
        return None


# ============================ payload.bin → boot.img ==========================
def extract_boot_from_zip(ota_zip, workdir):
    """從 OTA zip 抽出 payload.bin，再用 payload-dumper-go 抽 boot.img。"""
    payload = os.path.join(workdir, "payload.bin")
    rc, _, _ = sh(f'unzip -o -q "{ota_zip}" payload.bin -d "{workdir}"')
    if rc != 0 or not os.path.exists(payload):
        print("[x] 無法從 zip 抽出 payload.bin")
        return None
    outdir = os.path.join(workdir, "partitions")
    os.makedirs(outdir, exist_ok=True)
    rc, _, err = sh(f'{PDGO} -p boot -o "{outdir}" "{payload}"')
    if rc != 0:
        print(f"[x] payload-dumper-go 失敗: {err.strip()}")
        return None
    boot = os.path.join(outdir, "boot.img")
    return boot if os.path.exists(boot) else None


# ============================ boot.img → kernel Image ========================
def extract_kernel(boot_img, workdir):
    """解析 Android boot image header，抽出 kernel（必要時解壓縮）。"""
    data = open(boot_img, "rb").read()
    if data[:8] != b"ANDROID!":
        print("[x] 不是 Android boot image")
        return None
    kernel_size = struct.unpack("<I", data[8:12])[0]
    version = struct.unpack("<I", data[40:44])[0]
    header_size = 1584 if version in (3, 4) else (1632 if version == 0 else (1648 if version == 1 else 1660))
    page = 4096
    off = ((header_size + page - 1) // page) * page
    kernel = data[off:off + kernel_size]
    out = os.path.join(workdir, "kernel.Image")
    # 解壓縮
    if kernel[:2] == b"\x1f\x8b":
        import gzip
        kernel = gzip.decompress(kernel)
    elif kernel[:4] == b"\x02\x21\x4c\x18":
        print("[!] lz4 壓縮 kernel，需手動解壓（請提供 --boot 前先解壓）")
    elif kernel[:4] == b"\x28\xb5\x2f\xfd":
        print("[!] zstd 壓縮 kernel，需手動解壓")
    open(out, "wb").write(kernel)
    print(f"[+] kernel Image: {len(kernel)} bytes (header v{version})")
    return out


# ============================ Image → 符號 ===================================
def resolve_remove_waiter(kernel, workdir):
    """用 vmlinux-to-elf 還原符號，回傳 (base, remove_waiter_vma, next_vma)。"""
    elf = os.path.join(workdir, "kernel.elf")
    rc, out, err = sh(f'{VMLINUX_TO_ELF} "{kernel}" "{elf}"')
    if rc != 0:
        print(f"[x] vmlinux-to-elf 失敗: {err.strip()}")
        return None, None, None
    # base address 從 vmlinux-to-elf 輸出猜（形如 "base address (ffffffc008000000)"）
    base = None
    for line in (out + err).splitlines():
        m = re.search(r'base address[^()]*\(([0-9a-fA-F]{12,})\)', line)
        if m:
            base = int(m.group(1), 16)
    # 用 nm -n 取完整排序符號，找 remove_waiter 及下一個符號（作為函數邊界）
    rc, out, _ = sh(f'{NM} -n "{elf}"')
    vma = next_vma = None
    syms = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and re.fullmatch(r'[0-9a-fA-F]{8,16}', parts[0]):
            syms.append((int(parts[0], 16), parts[2]))
    for i, (addr, name) in enumerate(syms):
        if name == "remove_waiter":
            vma = addr
            if i + 1 < len(syms):
                next_vma = syms[i + 1][0]
            break
    return base, vma, next_vma


# ============================ 反組譯 + 判斷 ===================================
def disassemble_remove_waiter(kernel, base, vma, next_vma):
    import capstone
    img = open(kernel, "rb").read()
    if base is None:
        base = 0xffffffc008000000  # ARM64 常見 base
    fo = vma - base
    if next_vma:
        size = next_vma - vma
    else:
        size = 0x200  # 找不到下一個符號時，猜一個範圍
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_LITTLE_ENDIAN)
    code = img[fo:fo + size]
    insns = list(md.disasm(code, vma))
    return insns


def detect_patch(insns):
    """判斷 remove_waiter 是修補版(waiter->task) 還是漏洞版(current/sp_el0)。"""
    has_spel0 = any(i.mnemonic == "mrs" and "sp_el0" in i.op_str for i in insns)
    # 找 waiter->task 載入：ldr x?, [x_waiter, #0x30]
    has_waiter_task = any(
        i.mnemonic in ("ldr",) and f"#{WAITER_TASK_OFFSET:#x}" in i.op_str
        for i in insns
    )
    if has_spel0 and not has_waiter_task:
        return "VULNERABLE", "remove_waiter 使用 current（sp_el0）→ 未修補"
    if (not has_spel0) and has_waiter_task:
        return "PATCHED", "remove_waiter 使用 waiter->task → 已修補"
    return "UNCERTAIN", "無法自動判斷（需人工比對）"


# ============================ 報告 ===========================================
def report(ctx):
    R = []
    R.append("=" * 72)
    R.append(" GhostLock (CVE-2026-43499) Patch Check Report")
    R.append("=" * 72)
    R.append(f"檢查時間          : {ctx.get('time','')}")
    R.append(f"裝置目前版本      : {ctx.get('cur_ver','?')}  (SPL {ctx.get('cur_spl','?')})")
    R.append(f"分析 OTA 版本     : {ctx.get('ota_ver','?')}")
    R.append(f"kernel base       : {ctx.get('base','?')}")
    R.append(f"remove_waiter     : {ctx.get('vma','?')}")
    R.append("")
    R.append("--- 反組譯 remove_waiter ---")
    for i in ctx.get("insns", []):
        R.append(f"  {i.address:#x}: {i.mnemonic:8s} {i.op_str}")
    R.append("")
    R.append("--- 與官方 patch (commit 3bfdc63) 比對 ---")
    R.append("  官方修補：remove_waiter 的 current → waiter->task")
    R.append(f"  本 kernel 是否有 mrs sp_el0 (current 存取)：{'是' if ctx.get('spel0') else '否'}")
    R.append(f"  本 kernel 是否有 waiter->task 載入(offset 0x30)：{'是' if ctx.get('waiter_task') else '否'}")
    R.append("")
    R.append(f"  [判定] {ctx.get('verdict','?')}")
    R.append(f"  [說明] {ctx.get('verdict_desc','')}")
    R.append("")
    R.append("=" * 72)
    return "\n".join(R)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ota", help="對已下載的 OTA zip 分析")
    ap.add_argument("--boot", help="對已抽出的 boot.img 分析")
    args = ap.parse_args()

    ctx = {"time": time.strftime("%Y-%m-%d %H:%M:%S")}
    workdir = tempfile.mkdtemp(prefix="ghostlock_check.")

    # 裝置目前版本/SPL
    ctx["cur_ver"] = adb_getprop("ro.build.version.incremental") or "unknown"
    ctx["cur_spl"] = adb_getprop("ro.build.version.security_patch") or "unknown"
    print(f"[*] 裝置目前版本 = {ctx['cur_ver']} (SPL {ctx['cur_spl']})")

    boot = args.boot
    if args.ota:
        boot = extract_boot_from_zip(args.ota, workdir)
    elif boot is None:
        # 查最新版本 + 下載
        sys_ver = ctx["cur_ver"] if ctx["cur_ver"] != "unknown" else "1.22.009-20260710-150201"
        r = check_latest(sys_ver)
        if not r or r.get("code") != "OK":
            print("[x] 查詢最新版本失敗")
            return 1
        ctx["ota_ver"] = r.get("version", "?")
        print(f"[+] 最新版本 = {ctx['ota_ver']}, imageUrl = {r.get('imageUrl')}")
        if ctx["ota_ver"] == ctx["cur_ver"]:
            print("[i] 裝置已是最新，仍對其 kernel 分析")
        ota_zip = os.path.join(workdir, "ota.zip")
        print(f"[*] 下載 OTA ...")
        rc, _, err = sh(f'curl -fsSL -m 900 -o "{ota_zip}" "{r.get("imageUrl")}"')
        if rc != 0:
            print(f"[x] 下載失敗: {err.strip()}")
            return 1
        boot = extract_boot_from_zip(ota_zip, workdir)

    if not boot or not os.path.exists(boot):
        print("[x] 無 boot.img 可分析")
        return 1

    kernel = extract_kernel(boot, workdir)
    if not kernel:
        return 1

    base, vma, next_vma = resolve_remove_waiter(kernel, workdir)
    if vma is None:
        print("[x] 找不到 remove_waiter 符號")
        return 1
    ctx["base"] = hex(base or 0)
    ctx["vma"] = hex(vma)

    insns = disassemble_remove_waiter(kernel, base, vma, next_vma)
    ctx["insns"] = insns
    ctx["spel0"] = any(i.mnemonic == "mrs" and "sp_el0" in i.op_str for i in insns)
    ctx["waiter_task"] = any(i.mnemonic == "ldr" and f"#{WAITER_TASK_OFFSET:#x}" in i.op_str for i in insns)
    ctx["verdict"], ctx["verdict_desc"] = detect_patch(insns)

    rep = report(ctx)
    print(rep)
    out = os.path.join(os.getcwd(), "ghostlock_patch_report.txt")
    with open(out, "w") as f:
        f.write(rep + "\n")
    print(f"\n[+] 報告已寫入: {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
