#!/system/bin/sh
# GateServiced Protocol Explorer
# Try various commands to trigger TA loading

echo "=== GateServiced Protocol Explorer ==="
echo "Target: 127.0.0.1:8341"
echo ""

# Based on previous research:
# - Command format: [4-byte length BE] + [data]
# - Known error: ret:300c (TA not loaded)

# Function to send command and capture response
send_cmd() {
    local desc="$1"
    local hex="$2"
    echo "[$desc]"
    echo "  TX: $hex"
    echo "$hex" | xxd -r -p | nc -w 2 127.0.0.1 8341 2>/dev/null | xxd
    echo ""
}

# 1. Empty keepalive
echo "[1] Empty/Keepalive Commands:"
send_cmd "4-byte zero" "00000000"
send_cmd "8-byte zero" "0000000000000000"

# 2. Try potential "init" commands
echo "[2] Init/Connect Commands:"
send_cmd "Init 0x01" "0000000101"
send_cmd "Init 0x10" "0000000110"
send_cmd "GATE magic" "00000004GATE"

# 3. Try TZ-related commands
echo "[3] TZ/TA Commands:"
# Command 0x01 = version?
send_cmd "Cmd 0x01 (version?)" "00000005010000000000"
# Command 0x02 = init TA?
send_cmd "Cmd 0x02 (init?)" "00000005020000000000"
# Command 0x10 = load TA?
send_cmd "Cmd 0x10 (load?)" "00000005100000000000"
# Command 0x20 = query?
send_cmd "Cmd 0x20 (query?)" "00000005200000000000"

# 4. Try string-based commands
echo "[4] String Commands:"
send_cmd "load_ta" "0000000a6c6f61645f7461" # "load_ta"
send_cmd "init" "00000004696e6974" # "init"
send_cmd "connect" "00000007636f6e6e656374" # "connect"

# 5. Try QSEECom-like commands
echo "[5] QSEECom-like Commands:"
# QSEECom_start_app format guess
send_cmd "startApp" "00000008737461727441707000" # "startApp\0"
send_cmd "loadQuery" "000000096c6f616451756572790" # "loadQuery"

# 6. Analyze current state
echo "[6] Current TA State Check:"
# Try to get TA status
send_cmd "status" "00000006737461747573" # "status"
send_cmd "getState" "0000000867657453746174650" # "getState"

# 7. Binary protocol variants
echo "[7] Binary Protocol Variants:"
# Try different header formats
send_cmd "Alt Header 1" "00010000" 
send_cmd "Alt Header 2" "01000000"
send_cmd "Type+Len 1" "0100000800000000"
send_cmd "Type+Len 2" "0001000800000000"

echo "=== Protocol Exploration Complete ==="
