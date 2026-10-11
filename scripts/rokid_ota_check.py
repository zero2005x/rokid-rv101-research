#!/usr/bin/env python3
"""
Rokid OTA Firmware Check & Download Script
===========================================
Reconstructed from decompiled CheckService.java + DeviceProp.java

Authentication algorithm:
  sign = MD5("key={key}&device_type_id={dtid}&device_id={did}&service=ota&version=1.0&time={ts}&secret={secret}")
  Authorization: version=1.0;time={ts};sign={sign};key={key};device_type_id={dtid};device_id={did};service=ota

POST body:
  {"version":"<system_version>","osType":"","cpuType":""}
"""

import hashlib
import json
import time
import requests
import sys
import os

# ============================================================================
# Device credentials (from adb shell getprop)
# ============================================================================
KEY = "<REDACTED_OTA_KEY>"
SECRET = "<REDACTED_OTA_SECRET>"
DEVICE_TYPE_ID = "<REDACTED_OTA_DEVICETYPEID>"
DEVICE_ID = "<DEVICE_SERIAL>"  # ro.serialno
SERVICE = "ota"
VERSION = "1.0"  # DeviceProp.version is hardcoded to "1.0"

CHECK_URL = "https://ota.rokid.com"
CHECK_API = "/v1/extended/ota/check"

# System version from Build.FINGERPRINT parsing (DeviceUtil.getSystemVersion)
SYSTEM_VERSION = "1.12.009-20260109-150201"


def generate_signature(timestamp: int) -> str:
    """
    Reconstructed from CheckService.generateSignature():
    MD5("key={key}&device_type_id={dtid}&device_id={did}&service={svc}&version={ver}&time={ts}&secret={secret}")
    """
    sign_input = (
        f"key={KEY}"
        f"&device_type_id={DEVICE_TYPE_ID}"
        f"&device_id={DEVICE_ID}"
        f"&service={SERVICE}"
        f"&version={VERSION}"
        f"&time={timestamp}"
        f"&secret={SECRET}"
    )
    print(f"[*] Sign input: {sign_input}")
    md5_hash = hashlib.md5(sign_input.encode()).hexdigest()
    print(f"[*] MD5 sign: {md5_hash}")
    return md5_hash


def package_authorization(sign: str, timestamp: int) -> str:
    """
    Reconstructed from CheckService.packageAuthorization():
    "version={ver};time={ts};sign={sign};key={key};device_type_id={dtid};device_id={did};service={svc}"
    """
    auth = (
        f"version={VERSION}"
        f";time={timestamp}"
        f";sign={sign}"
        f";key={KEY}"
        f";device_type_id={DEVICE_TYPE_ID}"
        f";device_id={DEVICE_ID}"
        f";service={SERVICE}"
    )
    return auth


def package_body_data() -> str:
    """
    Reconstructed from CheckService.packageBodyData():
    {"version":"<systemVersion>","osType":"","cpuType":""}
    """
    body = {
        "version": SYSTEM_VERSION,
        "osType": "",
        "cpuType": ""
    }
    return json.dumps(body)


def check_ota():
    """Main OTA check function - mirrors CheckService.handleCheckOtaVersion()"""
    url = CHECK_URL + CHECK_API
    print("=" * 60)
    print("  Rokid OTA Firmware Check")
    print("=" * 60)
    print(f"[*] URL: {url}")
    print(f"[*] Device ID: {DEVICE_ID}")
    print(f"[*] Device Type: {DEVICE_TYPE_ID}")
    print(f"[*] System Version: {SYSTEM_VERSION}")
    print()

    # Generate timestamp and signature
    timestamp = int(time.time())
    print(f"[*] Timestamp: {timestamp}")

    sign = generate_signature(timestamp)
    authorization = package_authorization(sign, timestamp)
    body = package_body_data()

    print(f"[*] Authorization: {authorization}")
    print(f"[*] Body: {body}")
    print()

    # Make the request
    headers = {
        "Content-Type": "application/json;charset=utf-8",
        "Authorization": authorization,
    }

    print("[*] Sending POST request...")
    try:
        resp = requests.post(url, headers=headers, data=body, timeout=15)
        print(f"[*] HTTP Status: {resp.status_code}")
        print(f"[*] Response Headers:")
        for k, v in resp.headers.items():
            print(f"    {k}: {v}")
        print()

        response_text = resp.text
        print(f"[*] Response Body:")
        print(response_text)
        print()

        if resp.status_code != 200:
            print(f"[-] HTTP error: {resp.status_code}")
            return None

        # Parse response (from CheckService.unPackageResponse)
        try:
            data = json.loads(response_text)
            pretty = json.dumps(data, indent=2, ensure_ascii=False)
            print(f"[*] Parsed JSON:")
            print(pretty)
            print()

            code = data.get("code", "")
            if code.upper() == "OK":
                image_url = data.get("imageUrl", "")
                version = data.get("version", "")
                checksum = data.get("checksum", "")
                changelog = data.get("changelog", "")
                force_update = data.get("isForceUpdate", False)

                print("[+] ====== OTA UPDATE FOUND ======")
                print(f"[+] New Version: {version}")
                print(f"[+] Download URL: {image_url}")
                print(f"[+] Checksum (MD5): {checksum}")
                print(f"[+] Force Update: {force_update}")
                print(f"[+] Changelog: {changelog}")
                print()

                return {
                    "imageUrl": image_url,
                    "version": version,
                    "checksum": checksum,
                    "changelog": changelog,
                }
            else:
                error_msg = data.get("errorMessage", "unknown")
                print(f"[-] OTA check returned: code={code}, errorMessage={error_msg}")

                if code == "NO_IMAGE":
                    print("[*] No update available (device is on latest version)")
                else:
                    print(f"[-] Error code: {code}")
                return None

        except json.JSONDecodeError as e:
            print(f"[-] Failed to parse JSON: {e}")
            print(f"[-] Raw response: {response_text[:500]}")
            return None

    except requests.exceptions.RequestException as e:
        print(f"[-] Request failed: {e}")
        return None


def download_firmware(image_url: str, checksum: str = None):
    """Download the firmware file"""
    filename = image_url.split("/")[-1].split("?")[0]
    if not filename:
        filename = "rokid_firmware.zip"

    output_path = os.path.join(os.path.dirname(__file__), filename)
    print(f"[*] Downloading firmware to: {output_path}")

    try:
        resp = requests.get(image_url, stream=True, timeout=300)
        total_size = int(resp.headers.get("content-length", 0))
        print(f"[*] File size: {total_size / 1024 / 1024:.1f} MB")

        downloaded = 0
        md5_hash = hashlib.md5()

        with open(output_path, "wb") as f:
            for chunk in resp.iter_content(chunk_size=8192):
                if chunk:
                    f.write(chunk)
                    md5_hash.update(chunk)
                    downloaded += len(chunk)
                    if total_size > 0:
                        pct = downloaded * 100 / total_size
                        print(f"\r[*] Progress: {pct:.1f}% ({downloaded}/{total_size})", end="", flush=True)

        print()
        file_md5 = md5_hash.hexdigest()
        print(f"[+] Download complete: {output_path}")
        print(f"[+] File MD5: {file_md5}")

        if checksum:
            if file_md5 == checksum:
                print("[+] Checksum MATCHES!")
            else:
                print(f"[-] Checksum MISMATCH! Expected: {checksum}, Got: {file_md5}")

        return output_path

    except Exception as e:
        print(f"[-] Download failed: {e}")
        return None


def try_older_version():
    """Try requesting with an older version to force an update response"""
    global SYSTEM_VERSION
    old_versions = [
        "1.11.000-20250101-000000",
        "1.10.000-20240101-000000",
        "1.09.000-20230601-000000",
        "1.08.000-20230101-000000",
        "1.00.000-20220101-000000",
    ]

    for v in old_versions:
        print(f"\n{'='*60}")
        print(f"[*] Trying with spoofed version: {v}")
        print(f"{'='*60}")
        SYSTEM_VERSION = v
        result = check_ota()
        if result and result.get("imageUrl"):
            return result

    return None


if __name__ == "__main__":
    print()
    print("╔═══════════════════════════════════════════════════════════╗")
    print("║  Rokid OTA Firmware Checker                              ║")
    print("║  Based on decompiled CheckService.java                   ║")
    print("╚═══════════════════════════════════════════════════════════╝")
    print()

    # Step 1: Try with current version
    result = check_ota()

    # Step 2: If no update found, try older versions to get firmware URL
    if not result or not result.get("imageUrl"):
        print("\n[*] No update for current version. Trying older versions...")
        result = try_older_version()

    # Step 3: Download if we got a URL
    if result and result.get("imageUrl"):
        print(f"\n[+] Firmware URL obtained!")
        ans = input("[?] Download firmware now? (y/N): ").strip().lower()
        if ans == "y":
            download_firmware(result["imageUrl"], result.get("checksum"))
            print("\n[*] Next steps:")
            print("  1. Extract boot.img: payload-dumper-go -o output firmware.zip")
            print("  2. Transfer boot.img to phone, patch with Magisk")
            print("  3. fastboot flash boot_a magisk_patched_boot.img")
        else:
            print("[*] Skipping download. You can download manually from:")
            print(f"    {result['imageUrl']}")
    else:
        print("\n[-] Could not obtain firmware URL from any version.")
        print("[*] Alternative: try mitmproxy on the device's WiFi to intercept OTA traffic")
