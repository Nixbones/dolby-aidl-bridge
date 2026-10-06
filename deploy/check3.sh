echo "===A: файл-источник целиком (первые 3 строки libraries):"
sed -n '/<libraries>/,+3p' /data/adb/dolby/v2/vendor_audio_effects_config.xml
echo "===B: bind-путь (libraries):"
sed -n '/<libraries>/,+3p' /vendor/etc/audio_effects_config.xml
echo "===C: все effect-строки в bind-конфиге:"
grep -n "<effect " /vendor/etc/audio_effects_config.xml
echo "===D: HAL процесс:"
pidof android.hardware.audio.service-aidl.mediatek
echo "===E: HAL лог при старте (все, тег AHAL или effect):"
logcat -d 2>/dev/null | grep -iE "AHAL|effectfactory|effect_config|EffectsConfig" | tail -20
echo "===F: тест-дlopen шима из шелла:"
export LD_LIBRARY_PATH=/vendor/lib64/soundfx
/system/bin/linker64 --help >/dev/null 2>&1 && echo linker-ok
ls -la /vendor/lib64/soundfx/libdolbyaidlshim.so
echo "===G: попробуем загрузить шим toybox'ом через dlopen тест нет - проверим зависимости:"
grep -c NEEDED /dev/null 2>/dev/null
echo DONE3
