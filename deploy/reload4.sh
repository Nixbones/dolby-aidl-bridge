echo "[r4] перезапуск HAL и сбор логов..."
logcat -c 2>/dev/null
killall android.hardware.audio.service-aidl.mediatek 2>/dev/null
killall audioserver 2>/dev/null
sleep 6
echo "=== процессы:"
ps -A 2>/dev/null | grep -E "audioserver|audio.service-aidl" | head -4
echo "=== наши теги:"
logcat -d 2>/dev/null | grep -iE "DolbyAidlShim|dolbyaidlshim|libswdap|AELI" | tail -25
echo "=== ошибки загрузки либ фабрикой:"
logcat -d 2>/dev/null | grep -iE "AHAL_EffectFactory|EffectsFactory|dlopen|not found|can not find" | tail -25
echo "=== эффекты в аудиофлингере:"
dumpsys media.audio_flinger 2>/dev/null | grep -iE "Effect ID|9d4921da|dap" | head -12
echo "[r4] DONE"
