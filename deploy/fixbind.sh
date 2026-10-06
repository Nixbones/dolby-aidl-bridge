M1=/odm/etc/audio_effects_config.xml
M2=/vendor/etc/audio_effects_config.xml
F1=/data/adb/dolby/v2/odm_audio_effects_config.xml
F2=/data/adb/dolby/v2/vendor_audio_effects_config.xml
nsu() { nsenter --mount=/proc/1/ns/mnt -- "$@" 2>/dev/null || "$@"; }

echo "=== было (bind odm):"
grep -c "type=" $M1 2>/dev/null
echo "=== перемонтирую odm:"
nsu umount $M1 2>/dev/null
nsu mount -o bind $F1 $M1 || echo "FAIL odm"
echo "=== перемонтирую vendor:"
nsu umount $M2 2>/dev/null
nsu mount -o bind $F2 $M2 || echo "FAIL vendor"
echo "=== стало:"
grep -n "dap_mod" $M1 $M2
echo "=== рестарт аудио:"
logcat -c 2>/dev/null
killall android.hardware.audio.service-aidl.mediatek 2>/dev/null
killall audioserver 2>/dev/null
sleep 7
echo "=== ЛОГ:"
logcat -d 2>/dev/null | grep -iE "DolbyAidlShim|AELI|libswdap|AHAL_EffectFactory|nonProxyEffects|9d4921da" | tail -25
echo "=== число эффектов:"
logcat -d 2>/dev/null | grep -oE "with [0-9]+ nonProxyEffects" | tail -1
echo "[fixbind] DONE"
