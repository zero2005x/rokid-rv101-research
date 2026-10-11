#!/usr/bin/env python3
"""
Rokid OTA API Authentication Script
Based on reverse engineering of RokidOtaUpgrade.apk CheckService.java

Authentication mechanism:
1. Generate Unix timestamp (seconds)
2. Create signature string: "key={key}&device_type_id={deviceTypeId}&device_id={deviceId}&service={service}&version={version}&time={timestamp}&secret={secret}"
3. Calculate MD5 hash of signature string
4. Build Authorization header: "version={version};time={timestamp};sign={md5};key={key};device_type_id={deviceTypeId};device_id={deviceId};service={service}"
5. POST JSON body: {"version": "{system_version}", "osType": "", "cpuType": ""}
"""

import hashlib
import json
import time
import requests
import sys

# Device-specific parameters (from adb shell getprop)
DEVICE_PARAMS = {
    "key": "<REDACTED_OTA_KEY>",          # ro.boot.key
    "secret": "<REDACTED_OTA_SECRET>",       # ro.boot.secret
    "device_type_id": "<REDACTED_OTA_DEVICETYPEID>", # ro.boot.devicetypeid
    "device_id": "<DEVICE_SERIAL>",                    # ro.serialno
    "version": "1.0",                                   # fixed value from DeviceProp
    "service": "ota",                                   # fixed value from DeviceProp
}

OTA_URL = "https://ota.rokid.com/v1/extended/ota/check"

# Current system version on device (from Build.FINGERPRINT parsing)
# Fingerprint: Rokid/glasses/glasses:12/SKQ1.240613.001/1.12.009-20260109-150201:user/release-keys
# Split by ":" -> get [1] -> split by "/" -> get last element
SYSTEM_VERSION = "1.12.009-20260109-150201"

def md5_hash(s):
    """Calculate MD5 hash and return lowercase hex string"""
    return hashlib.md5(s.encode('utf-8')).hexdigest()

def generate_signature(timestamp):
    """
    Generate signature as per CheckService.generateSignature()
    
    Format: key={key}&device_type_id={deviceTypeId}&device_id={deviceId}&service={service}&version={version}&time={timestamp}&secret={secret}
    """
    sig_string = (
        f"key={DEVICE_PARAMS['key']}"
        f"&device_type_id={DEVICE_PARAMS['device_type_id']}"
        f"&device_id={DEVICE_PARAMS['device_id']}"
        f"&service={DEVICE_PARAMS['service']}"
        f"&version={DEVICE_PARAMS['version']}"
        f"&time={timestamp}"
        f"&secret={DEVICE_PARAMS['secret']}"
    )
    print(f"[DEBUG] Signature string: {sig_string}")
    return md5_hash(sig_string)

def package_authorization(signature, timestamp):
    """
    Package Authorization header as per CheckService.packageAuthorization()
    
    Format: version={version};time={timestamp};sign={signature};key={key};device_type_id={deviceTypeId};device_id={deviceId};service={service}
    """
    return (
        f"version={DEVICE_PARAMS['version']}"
        f";time={timestamp}"
        f";sign={signature}"
        f";key={DEVICE_PARAMS['key']}"
        f";device_type_id={DEVICE_PARAMS['device_type_id']}"
        f";device_id={DEVICE_PARAMS['device_id']}"
        f";service={DEVICE_PARAMS['service']}"
    )

def package_body_data(system_version=None):
    """
    Package request body as per CheckService.packageBodyData()
    """
    if system_version is None:
        system_version = SYSTEM_VERSION
    return json.dumps({
        "version": system_version,
        "osType": "",
        "cpuType": ""
    })

def check_ota_update(system_version=None):
    """
    Perform OTA update check with proper authentication
    """
    # Generate timestamp (Unix seconds)
    timestamp = int(time.time())
    print(f"[INFO] Timestamp: {timestamp}")
    
    # Generate signature
    signature = generate_signature(timestamp)
    print(f"[INFO] Signature (MD5): {signature}")
    
    # Build Authorization header
    auth_header = package_authorization(signature, timestamp)
    print(f"[INFO] Authorization: {auth_header}")
    
    # Build request body
    body = package_body_data(system_version)
    print(f"[INFO] Body: {body}")
    
    # Prepare headers
    headers = {
        "Content-Type": "application/json;charset=utf-8",
        "Authorization": auth_header,
        "User-Agent": "okhttp/4.9.3",  # Match Android OkHttp
    }
    
    print(f"\n[INFO] Sending POST to {OTA_URL}")
    print(f"[INFO] Headers: {json.dumps(headers, indent=2)}")
    
    try:
        response = requests.post(
            OTA_URL,
            headers=headers,
            data=body,
            timeout=30
        )
        
        print(f"\n[RESPONSE] Status Code: {response.status_code}")
        print(f"[RESPONSE] Headers: {dict(response.headers)}")
        print(f"[RESPONSE] Body: {response.text}")
        
        # Parse response
        try:
            resp_json = response.json()
            print(f"\n[PARSED] JSON Response:")
            print(json.dumps(resp_json, indent=2, ensure_ascii=False))
            
            if resp_json.get("code") == "OK":
                print("\n[SUCCESS] OTA update available!")
                print(f"  Version: {resp_json.get('version')}")
                print(f"  Image URL: {resp_json.get('imageUrl')}")
                print(f"  Checksum: {resp_json.get('checksum')}")
                print(f"  Force Update: {resp_json.get('isForceUpdate')}")
                return resp_json
            else:
                print(f"\n[INFO] Code: {resp_json.get('code')}")
                print(f"[INFO] Message: {resp_json.get('errorMessage', resp_json.get('message'))}")
                return resp_json
                
        except json.JSONDecodeError:
            print(f"[ERROR] Could not parse JSON response")
            return None
            
    except requests.exceptions.RequestException as e:
        print(f"[ERROR] Request failed: {e}")
        return None

def test_with_older_version():
    """Test with an older version to force update availability"""
    print("\n" + "="*60)
    print("Testing with older version (1.10.000)")
    print("="*60)
    return check_ota_update("1.10.000")

def test_with_current_version():
    """Test with current device version"""
    print("\n" + "="*60)
    print(f"Testing with current version ({SYSTEM_VERSION})")
    print("="*60)
    return check_ota_update(SYSTEM_VERSION)

if __name__ == "__main__":
    print("="*60)
    print("Rokid OTA API Authentication Test")
    print("="*60)
    print(f"\nDevice Parameters:")
    for k, v in DEVICE_PARAMS.items():
        if k == "secret":
            print(f"  {k}: {v[:8]}...{v[-4:]} (hidden)")
        else:
            print(f"  {k}: {v}")
    print(f"  system_version: {SYSTEM_VERSION}")
    print()
    
    # Test with current version first
    result = test_with_current_version()
    
    # If no update found, try older version
    if result and result.get("code") == "NO_IMAGE":
        result = test_with_older_version()
    elif result and result.get("code") != "OK":
        # Try older version anyway
        result = test_with_older_version()
    
    print("\n" + "="*60)
    print("Test Complete")
    print("="*60)
