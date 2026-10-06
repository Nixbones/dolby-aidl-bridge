S=/data/adb/dolby/v2/vendor_soundfx
P=/data/adb/dolby/payload/vendor/lib64
echo "[sync2] копирую служебные либы в soundfx..."
for f in libutdlb.so libhidldlbs.so libstagefright_fdtn_dolby.so libdapparamstorage.so libdlbpreg.so libdlbdsservice.so libsqlite.so vendor.dolby.hardware.dms@2.0.so vendor.dolby.hardware.dms@2.0-impl.so; do
  cp -f "$P/$f" "$S/$f" 2>/dev/null && echo "  + $f"
done
cp -f /data/local/tmp/dolby/newshim.so "$S/libdolbyaidlshim.so"
chmod 644 "$S"/*.so
echo "[sync2] пробник (узкий LD_LIBRARY_PATH):"
export LD_LIBRARY_PATH=/vendor/lib64/soundfx:/system/lib64
/data/local/tmp/dolby/dtest
echo "[sync2] DONE"
