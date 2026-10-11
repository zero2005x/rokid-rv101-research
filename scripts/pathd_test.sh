#!/system/bin/sh
LOG=/data/local/tmp/pathd_test.log
exec > "$LOG" 2>&1
P=/sys/devices/platform/soc/a90000.i2c/i2c-1/1-0008

echo "=== PATH D TEST START ($(date)) ==="
id
getenforce
echo ""

echo "=== BASELINE (camera idle) ==="
echo -n "brightness="; cat /sys/class/leds/white/brightness 2>&1
echo -n "led_current="; cat /sys/class/leds/white/led_current 2>&1
echo -n "is_take_on="; getprop vendor.rkd.glasses.is_take_on
echo -n "is_spread="; getprop vendor.rkd.glasses.is_spread
echo -n "session_open=["; getprop vendor.rkd.camera.session_open; echo "]"
echo -n "enforce_psensor(sysfs)="; cat $P/enforce_psensor 2>&1
echo -n "thres_pct="; cat $P/thres_pct 2>&1
echo -n "psensor="; cat $P/psensor 2>&1
echo -n "psensor0="; cat $P/psensor0 2>&1
echo -n "hall="; cat $P/hall 2>&1
echo ""

echo "########## STEP 2: SIMULATE CAMERA-ACTIVE BASELINE ##########"
echo "--- setprop session_open 1 ---"; setprop vendor.rkd.camera.session_open 1; echo "rc=$?"
echo "--- service call lights_ctrl 8 (sendEvent camera-open) ---"; service call lights_ctrl 8 i32 2 i32 2014 2>&1
sleep 1
echo "--- LED after camera-active baseline ---"
echo -n "brightness="; cat /sys/class/leds/white/brightness 2>&1
dumpsys lights_ctrl 2>&1 | grep -A5 "mCurrentEvent"
echo ""

echo "########## STEP 3: SUPPRESS LED VIA P-SENSOR / WEARING-STATE SPOOF ##########"
echo "--- try sysfs is_take_on node (expected absent) ---"
( echo 0 > /sys/class/sensors/proximity/is_take_on ) 2>&1 || echo "node not found (as expected)"
echo "--- forge wearing/folding state via properties ---"
setprop vendor.rkd.glasses.is_take_on 0; echo "set is_take_on=0 rc=$?"
setprop vendor.rkd.glasses.is_spread 0; echo "set is_spread=0 rc=$?"
echo "--- forge psensor enforcement OFF via property (init.rc -> PSOC sysfs) ---"
setprop rokid.debug.enforce_psensor 0; echo "set enforce_psensor=0 rc=$?"
sleep 2
echo "--- LED state after ALL state forgery ---"
echo -n "brightness="; cat /sys/class/leds/white/brightness 2>&1
dumpsys lights_ctrl 2>&1 | grep -A5 "mCurrentEvent"
echo "--- properties/sysfs after forgery (check overwrite) ---"
echo -n "is_take_on="; getprop vendor.rkd.glasses.is_take_on
echo -n "is_spread="; getprop vendor.rkd.glasses.is_spread
echo -n "enforce_psensor(sysfs)="; cat $P/enforce_psensor 2>&1
echo "--- dmesg transitions ---"
dmesg 2>&1 | tail -12 | grep -iE "sensor|take_on|spread|camera|led|psensor|enforce|proximity"
echo ""

echo "########## STEP 4: RESTORE & VERIFY ##########"
setprop vendor.rkd.glasses.is_take_on 1; echo "restore is_take_on=1 rc=$?"
setprop vendor.rkd.glasses.is_spread 1; echo "restore is_spread=1 rc=$?"
setprop rokid.debug.enforce_psensor 1; echo "restore enforce_psensor=1 rc=$?"
service call lights_ctrl 9 i32 2 i32 2014 2>&1
setprop vendor.rkd.camera.session_open ""; echo "clear session_open rc=$?"
sleep 1
echo "--- final state ---"
echo -n "brightness="; cat /sys/class/leds/white/brightness 2>&1
echo -n "is_take_on="; getprop vendor.rkd.glasses.is_take_on
echo -n "is_spread="; getprop vendor.rkd.glasses.is_spread
echo -n "session_open=["; getprop vendor.rkd.camera.session_open; echo "]"
echo -n "enforce_psensor(sysfs)="; cat $P/enforce_psensor 2>&1
dumpsys lights_ctrl 2>&1 | grep -A5 "mCurrentEvent"

echo ""
echo "=== PATH_D_TEST_DONE ==="
