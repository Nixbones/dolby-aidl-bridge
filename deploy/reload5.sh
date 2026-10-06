echo "[r5] перезапуск аудио-стекa..."
logcat -c 2>/dev/null
killall android.hardware.audio.service-aidl.mediatek 2>/dev/null
killall audioserver 2>/dev/null
sleep 6
echo "=== наши логи (DolbyAidlShim/AELI):"
logcat -d 2>/dev/null | grep -iE "DolbyAidlShim|AELI|libswdap|dap_mod" | tail -20
echo "=== фабрика:"
logcat -d 2>/dev/null | grep -iE "EffectsFactoryHalAidl|AHAL_EffectFactory|EffectConfig" | tail -10
echo "=== число эффектов в HAL:"
logcat -d 2>/dev/null | grep -oE "with [0-9]+ nonProxyEffects" | tail -2
echo "=== эффект dap виден системе?"
dumpsys media.audio_flinger 2>/dev/null | grep -iE "9d4921da|dap" | head -6
echo "=== все Effect ID:"
dumpsys media.audio_flinger 2>/dev/null | grep -c "Effect ID"
echo "[r5] DONE"
