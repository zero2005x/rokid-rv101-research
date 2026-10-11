#!/system/bin/sh
# GateServiced binary command sender

# Create binary command file
CMD_FILE="/data/local/tmp/gate_cmd.bin"

# Write binary data - 12 bytes: length(4) + cmd(4) + padding(4)
printf '\x00\x00\x00\x08\x00\x00\x00\x01\x00\x00\x00\x00' > $CMD_FILE

echo "Sending command to GateServiced..."
echo "Command hex:"
xxd $CMD_FILE

echo ""
echo "Response:"
cat $CMD_FILE | nc 127.0.0.1 8341 | xxd &
NC_PID=$!
sleep 3
kill $NC_PID 2>/dev/null

echo ""
echo "Testing with different commands..."

# Test command 2
printf '\x00\x00\x00\x08\x00\x00\x00\x02\x00\x00\x00\x00' > $CMD_FILE
echo "Cmd 0x02:"
cat $CMD_FILE | nc 127.0.0.1 8341 | xxd &
NC_PID=$!
sleep 2
kill $NC_PID 2>/dev/null

# Test longer command
printf '\x00\x00\x00\x10\x00\x00\x00\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > $CMD_FILE
echo "Cmd 0x03 (16 bytes):"
cat $CMD_FILE | nc 127.0.0.1 8341 | xxd &
NC_PID=$!
sleep 2
kill $NC_PID 2>/dev/null

rm -f $CMD_FILE
echo "Done"
