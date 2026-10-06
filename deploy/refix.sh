echo "[refix] перепатчиваю конфиги с type-атрибутом..."
for F in /data/adb/dolby/v2/vendor_audio_effects_config.xml /data/adb/dolby/v2/odm_audio_effects_config.xml; do
  [ -f "$F" ] || continue
  sed -i '/dap_mod/d; /dap_shim/d' "$F"
  sed -i '/<libraries>/a\
        <library name="dap_shim" path="libdolbyaidlshim.so"/>' "$F"
  sed -i '/<effects>/a\
        <effect name="dap_mod" library="dap_shim" uuid="9d4921da-8225-4f29-aefa-39537a04bcaa" type="e119e520-7dfc-4a15-b1fa-234a0939f082"/>' "$F"
  echo "[refix] $F:"
  grep -n "dap_shim\|dap_mod" "$F"
done
logcat -c 2>/dev/null
killall android.hardware.audio.service-aidl.mediatek 2>/dev/null
killall audioserver 2>/dev/null
sleep 5
echo "[refix] лог загрузки эффекта:"
logcat -d 2>/dev/null | grep -iE "EffectFactory|EffectsFactory|EffectConfig|dolbyaidlshim|libswdap|DolbyAidlShim|AELI|dap_mod|can not find type" | tail -30
echo "[refix] DONE"
