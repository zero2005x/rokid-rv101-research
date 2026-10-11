#!/usr/bin/env python3
# verify_gmaps_api.py

import os
import sys
import json
import argparse
import requests


def main():
    ap = argparse.ArgumentParser(description="Quickly verify Google Maps API key via Geocoding API.")
    ap.add_argument("--key", default=os.getenv("GOOGLE_MAPS_API_KEY"),
                help="Google Maps API key (or env GOOGLE_MAPS_API_KEY)")
    ap.add_argument("--address", default="台北101", help="Address to geocode (default: 台北101)")
    ap.add_argument("--timeout", type=float, default=10.0, help="HTTP timeout seconds (default: 10)")
    args = ap.parse_args()

    if not args.key:
        print("ERROR: Missing API key. Provide --key or set env GOOGLE_MAPS_API_KEY.", file=sys.stderr)
        sys.exit(2)

    url = "https://maps.googleapis.com/maps/api/geocode/json"
    params = {
        "address": args.address,
        "key": args.key,
    }

    try:
        r = requests.get(url, params=params, timeout=args.timeout)
    except requests.RequestException as e:
        print(f"ERROR: Network/HTTP request failed: {e}", file=sys.stderr)
        sys.exit(3)

    print(f"HTTP {r.status_code}")
    ctype = r.headers.get("content-type", "")
    if "application/json" not in ctype:
        print("ERROR: Non-JSON response (unexpected). First 500 bytes:", file=sys.stderr)
        print(r.text[:500], file=sys.stderr)
        sys.exit(4)

    try:
        data = r.json()
    except ValueError:
        print("ERROR: JSON parse failed. First 500 bytes:", file=sys.stderr)
        print(r.text[:500], file=sys.stderr)
        sys.exit(5)

    status = data.get("status")
    err = data.get("error_message")
    print(f"Geocoding status: {status}")
    if err:
        print(f"error_message: {err}")

    if status == "OK" and data.get("results"):
        top = data["results"][0]
        fmt = top.get("formatted_address")
        loc = (top.get("geometry", {}).get("location") or {})
        lat, lng = loc.get("lat"), loc.get("lng")
        print(f"Top result: {fmt}")
        print(f"Location: lat={lat}, lng={lng}")
        sys.exit(0)

    # Helpful extra output for debugging
    print("Raw response (trimmed):")
    print(json.dumps(data, ensure_ascii=False, indent=2)[:2000])
    sys.exit(1)


if __name__ == "__main__":
    main()
