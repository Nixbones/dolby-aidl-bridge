echo "=== A: наш эффект в источнике:"
grep -n "dap" /data/adb/dolby/v2/odm_audio_effects_config.xml
echo "=== B: через bind-путь:"
grep -n "dap" /odm/etc/audio_effects_config.xml
echo "=== C: libraries в bind-файле (первые 4):"
sed -n '/<libraries>/,+4p' /odm/etc/audio_effects_config.xml
echo "=== D: полный лог AHAL_EffectFactory (с начала буфера):"
logcat -d -b all 2>/dev/null | grep -iE "AHAL_EffectFactory|EffectConfig|EffectFactory" | head -30
echo "=== E: предупреждения парсинга:"
logcat -d -b all 2>/dev/null | grep -iE "can not find|skipping|invalid|not exist" | head -15
echo "=== F: какие процессы и их конфиг (открытые файлы):"
for p in $(pidof android.hardware.audio.service-aidl.mediatek audioserver); do echo "pid $p:"; ls -l /proc/$p/fd 2>/dev/null | grep -c ""; done
echo "=== G: media.audio_flinger всего эффектов:"
dumpsys media.audio_flinger 2>/dev/null | grep -cE "Effect ID"
echo DONE5
