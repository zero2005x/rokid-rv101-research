#!/system/bin/sh
# TZAccessService Bind Test Script
# Purpose: Test binding to TZAccessService using different methods

echo "=== TZAccessService Bind Test ==="
echo "Target: com.qualcomm.qti.qms.service.trustzoneaccess/.TZAccessService"
echo ""

# Method 1: Try to bind using broadcast
echo "[1] Sending broadcast to trigger service..."
am broadcast -a com.qualcomm.qti.qms.service.trustzoneaccess.TZAccessService 2>&1

# Method 2: Start as background service (not foreground)
echo ""
echo "[2] Starting as background service..."
am startservice -n com.qualcomm.qti.qms.service.trustzoneaccess/.TZAccessService 2>&1

# Method 3: Query service status
echo ""
echo "[3] Checking service status..."
dumpsys activity services com.qualcomm.qti.qms.service.trustzoneaccess 2>&1 | head -30

# Method 4: Check if there's any IPC available
echo ""
echo "[4] Checking available binder interfaces..."
service list 2>/dev/null | grep -iE 'qti|qms|trust|tz'

# Method 5: Look for any AIDL interfaces
echo ""
echo "[5] Checking content providers..."
content query --uri content://com.qualcomm.qti.qms.service.trustzoneaccess/ 2>&1 | head -10

# Method 6: Check for intent filters
echo ""
echo "[6] Package intent filters..."
dumpsys package com.qualcomm.qti.qms.service.trustzoneaccess 2>&1 | grep -A5 "Intent"

echo ""
echo "=== Test Complete ==="
