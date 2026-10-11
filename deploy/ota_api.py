#!/usr/bin/env python3
"""
ota_api.py — query the Rokid OTA version-check API directly with the cracked signature.

Signature algorithm (reversed from com.rokid.glass.ota.CheckService):
  sign = MD5("key=<key>&device_type_id=<dtid>&device_id=<did>&service=<service>&version=<v>&time=<ts>&secret=<secret>")
  Authorization = "version=<v>;time=<ts>;sign=<sign>;key=<key>;device_type_id=<dtid>;device_id=<did>;service=<service>"
  Body = {"version": <systemVersion>, "osType": "", "cpuType": ""}

The response carries the latest version / imageUrl / checksum / changelog / isForceUpdate.

Usage:
  python3 ota_api.py                        # use the default device values
  SYSTEM_VERSION=1.22.009-20260710-150201 python3 ota_api.py
"""
import hashlib
import json
import os
import sys
import time
import urllib.request
import urllib.error

KEY = os.environ.get("KEY", "<REDACTED_OTA_KEY>")
SECRET = os.environ.get("SECRET", "<REDACTED_OTA_SECRET>")
DEVICE_TYPE_ID = os.environ.get("DEVICE_TYPE_ID", "<REDACTED_OTA_DEVICETYPEID>")
DEVICE_ID = os.environ.get("DEVICE_ID", "<DEVICE_SERIAL>")
SERVICE = os.environ.get("SERVICE", "ota")
VERSION = os.environ.get("VERSION", "1.0")
SYSTEM_VERSION = os.environ.get("SYSTEM_VERSION", "1.22.009-20260710-150201")
CHECK_URL = os.environ.get("CHECK_URL", "https://ota.rokid.com")
CHECK_API = os.environ.get("CHECK_API", "/v1/extended/ota/check")


def make_sign(ts: int) -> str:
    s = (f"key={KEY}&device_type_id={DEVICE_TYPE_ID}&device_id={DEVICE_ID}"
         f"&service={SERVICE}&version={VERSION}&time={ts}&secret={SECRET}")
    return hashlib.md5(s.encode()).hexdigest()


def check() -> dict:
    ts = int(time.time())
    sign = make_sign(ts)
    auth = (f"version={VERSION};time={ts};sign={sign};key={KEY};"
            f"device_type_id={DEVICE_TYPE_ID};device_id={DEVICE_ID};service={SERVICE}")
    body = json.dumps({"version": SYSTEM_VERSION, "osType": "", "cpuType": ""})
    url = CHECK_URL + CHECK_API
    req = urllib.request.Request(url, data=body.encode(), method="POST")
    req.add_header("Content-type", "application/json;charset=utf-8")
    req.add_header("Authorization", auth)
    print(f"[*] POST {url}", file=sys.stderr)
    print(f"[*] Authorization: {auth}", file=sys.stderr)
    print(f"[*] Body: {body}", file=sys.stderr)
    try:
        resp = urllib.request.urlopen(req, timeout=20)
        return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        print(f"[x] HTTP {e.code}: {e.read().decode()}", file=sys.stderr)
        raise


def main():
    r = check()
    print(json.dumps(r, ensure_ascii=False, indent=2))
    if r.get("code") == "OK":
        latest = r.get("version", "")
        print(f"\n[+] 最新版本 = {latest}", file=sys.stderr)
        print(f"[+] imageUrl = {r.get('imageUrl')}", file=sys.stderr)
        print(f"[+] checksum = {r.get('checksum')}", file=sys.stderr)
        if latest != SYSTEM_VERSION:
            print(f"[!] 有更新：目前 {SYSTEM_VERSION} -> 最新 {latest}", file=sys.stderr)
        else:
            print("[+] 已是最新。", file=sys.stderr)


if __name__ == "__main__":
    main()
