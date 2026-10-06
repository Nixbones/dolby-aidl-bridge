echo "[dtest] запуск пробника:"
export LD_LIBRARY_PATH=/vendor/lib64/soundfx:/vendor/lib64:/odm/lib64:/system/lib64
/data/local/tmp/dolby/dtest
echo "[dtest] ret=$?"
echo "=== logcat DolbyAidlShim:"
logcat -d 2>/dev/null | grep -iE "DolbyAidlShim|dolbyaidlshim" | tail -15
echo "[dtest] DONE"
