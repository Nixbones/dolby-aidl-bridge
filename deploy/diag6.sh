echo "=== процессы:"
ps -A 2>/dev/null | grep -iE "audio" | head -5
P=$(pidof android.hardware.audio.service-aidl.mediatek)
echo "HAL pid: $P"
echo "=== лог HAL (по pid):"
logcat -d 2>/dev/null | grep -E " $P " | tail -30
echo "=== логи фабрики (любые):"
logcat -d -b all 2>/dev/null | grep -iE "AHAL_EffectFactory|EffectConfig|loading lib" | tail -20
echo "=== краши:"
logcat -d 2>/dev/null | grep -iE "Fatal signal|tombstone|DEBUG   :" | tail -8
echo "=== права:"
ls -ld /data/adb/dolby /data/adb/dolby/v2
ls -la /data/adb/dolby/v2/*.xml /data/adb/dolby/v2/vendor_soundfx/libdolbyaidlshim.so 2>&1 | head -6
echo "=== монтирования:"
mount 2>/dev/null | grep -E "audio_effects|soundfx|lib64" | awk '{print $1, $3, $5}'
echo DONE6
