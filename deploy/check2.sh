echo ===1-vnutri-fayla:
grep -c dap_shim /data/adb/dolby/v2/vendor_audio_effects_config.xml
echo ===2-cherez-bind:
grep -c dap_shim /vendor/etc/audio_effects_config.xml
grep -c dap_shim /odm/etc/audio_effects_config.xml
echo ===3-maunty:
mount | grep audio_effects
mount | grep odm/lib64
mount | grep soundfx
echo ===4-odm-lib64:
ls /odm/lib64/ | head -8
echo ===5-libi-shim:
ls -la /vendor/lib64/soundfx/libdolbyaidlshim.so /vendor/lib64/soundfx/libswdap.so /odm/lib64/libutdlb.so 2>&1
echo ===DONE
