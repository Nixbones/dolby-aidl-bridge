#!/system/bin/sh
# =============================================================================
# Dolby Atmos deploy v2 для RMX5085 - без overlayfs (EROFS!)
# Тактика: bind-маунты директорий /odm/lib64 и /vendor/lib64/soundfx
#          + bind файлов патченных AIDL-конфигов.
# Запуск:  sh /data/local/tmp/dolby/deploy2.sh     (от root)
# Откат:   sh /data/local/tmp/dolby/deploy2.sh undo  или перезагрузка
# =============================================================================
BASE=/data/adb/dolby
PAY=$BASE/payload
WRK=$BASE/v2
MODE="${1:-go}"

log() { echo "[dolby2] $*"; }
fail() { echo "[dolby2] ОШИБКА: $*"; }

do_mount() {
  nsenter --mount=/proc/1/ns/mnt -- mount "$@" 2>&1 && return 0
  log "  (nsenter не сработал, пробую напрямую)"
  mount "$@" 2>&1
}
do_umount() {
  nsenter --mount=/proc/1/ns/mnt -- umount "$@" 2>/dev/null && return 0
  umount "$@" 2>/dev/null
}

if [ "$(id -u)" != "0" ]; then fail "нужен root"; exit 1; fi
[ -d "$PAY/vendor" ] || { fail "нет payload"; exit 1; }

if [ "$MODE" = "undo" ]; then
  do_umount /vendor/etc/audio_effects_config.xml
  do_umount /odm/etc/audio_effects_config.xml
  do_umount /vendor/lib64/soundfx
  do_umount /odm/lib64
  log "маунты сняты"
  exit 0
fi

# SELinux permissive (нужно для загрузки своих либ)
if [ "$(getenforce)" != "Permissive" ]; then
  setenforce 0 && log "SELinux -> Permissive" || fail "setenforce 0 не сработал"
fi

mkdir -p "$WRK"

# ============ 1. /odm/lib64 = копия системных + наши служебные ============
ODMDIR=$WRK/odm_lib64
if ! mount 2>/dev/null | grep -q "on /odm/lib64 "; then
  log "готовлю /odm/lib64..."
  rm -rf "$ODMDIR"; mkdir -p "$ODMDIR"
  cp -a /odm/lib64/. "$ODMDIR"/ 2>/dev/null
  cp -f "$PAY"/vendor/lib64/libutdlb.so "$ODMDIR"/
  cp -f "$PAY"/vendor/lib64/libhidldlbs.so "$ODMDIR"/
  cp -f "$PAY"/vendor/lib64/libstagefright_fdtn_dolby.so "$ODMDIR"/
  cp -f "$PAY"/vendor/lib64/libdapparamstorage.so "$ODMDIR"/
  cp -f "$PAY"/vendor/lib64/libdlbpreg.so "$ODMDIR"/
  cp -f "$PAY"/vendor/lib64/libdlbdsservice.so "$ODMDIR"/
  cp -f "$PAY"/vendor/lib64/libsqlite.so "$ODMDIR"/
  cp -f "$PAY"/vendor/lib64/vendor.dolby.hardware.dms@2.0.so "$ODMDIR"/
  cp -f "$PAY"/vendor/lib64/vendor.dolby.hardware.dms@2.0-impl.so "$ODMDIR"/
  cp -f "$PAY"/vendor/lib64/soundfx/libswdap.so "$ODMDIR"/
  cp -f "$PAY"/vendor/lib64/soundfx/libdolbyaidlshim.so "$ODMDIR"/
  chmod 644 "$ODMDIR"/*.so
  log "  bind /odm/lib64..."
  do_mount -o bind "$ODMDIR" /odm/lib64 && log "  ok" || { fail "bind odm/lib64"; exit 1; }
else
  log "/odm/lib64 уже смонтирован"
fi

# ============ 2. /vendor/lib64/soundfx = копия системных + swdap + шим ============
SFDIR=$WRK/vendor_soundfx
if ! mount 2>/dev/null | grep -q "on /vendor/lib64/soundfx "; then
  log "готовлю /vendor/lib64/soundfx..."
  rm -rf "$SFDIR"; mkdir -p "$SFDIR"
  cp -a /vendor/lib64/soundfx/. "$SFDIR"/ 2>/dev/null
  cp -f "$PAY"/vendor/lib64/soundfx/libswdap.so "$SFDIR"/
  cp -f "$PAY"/vendor/lib64/soundfx/libdolbyaidlshim.so "$SFDIR"/
  chmod 644 "$SFDIR"/*.so
  log "  bind /vendor/lib64/soundfx..."
  do_mount -o bind "$SFDIR" /vendor/lib64/soundfx && log "  ok" || { fail "bind soundfx"; exit 1; }
else
  log "/vendor/lib64/soundfx уже смонтирован"
fi

# ============ 3. патч и bind AIDL-конфигов ============
patch_cfg() {
  SRC="$1"; DST="$2"
  [ -f "$SRC" ] || return 1
  cp -f "$SRC" "$DST"
  sed -i '/9d4921da-8225-4f29-aefa-39537a04bcaa/d; /dap_mod/d; /dap_shim/d' "$DST"
  sed -i '/<libraries>/a\
        <library name="dap_shim" path="libdolbyaidlshim.so"/>' "$DST"
  sed -i '/<effects>/a\
        <effect name="dap_mod" library="dap_shim" uuid="9d4921da-8225-4f29-aefa-39537a04bcaa"/>' "$DST"
  return 0
}

log "патчу vendor AIDL-конфиг..."
patch_cfg /vendor/etc/audio_effects_config.xml "$WRK/vendor_audio_effects_config.xml" \
  && do_mount -o bind "$WRK/vendor_audio_effects_config.xml" /vendor/etc/audio_effects_config.xml \
  && log "  ok" || fail "  vendor конфиг"
log "патчу odm AIDL-конфиг..."
patch_cfg /odm/etc/audio_effects_config.xml "$WRK/odm_audio_effects_config.xml" \
  && do_mount -o bind "$WRK/odm_audio_effects_config.xml" /odm/etc/audio_effects_config.xml \
  && log "  ok" || fail "  odm конфиг"

# ============ 4. проперти ============
set_prop() {
  resetprop -n "$1" "$2" 2>/dev/null || setprop "$1" "$2" 2>/dev/null
}
set_prop ro.audio.ignore_effects false
set_prop ro.vendor.dolby.dax.version DAX3_3.7.0.8_r1
set_prop ro.dolby.mod_uuid false
set_prop ro.dolby.music_stream false

# каталоги данных
mkdir -p /data/vendor/dolby /data/vendor/multimedia 2>/dev/null
chown media:media /data/vendor/dolby 2>/dev/null; chmod 0770 /data/vendor/dolby 2>/dev/null

# ============ 5. рестарт аудио ============
log "перезапускаю audioserver + effect HAL..."
killall audioserver 2>/dev/null
ps -A 2>/dev/null | grep "audio.effect" | grep -v grep | awk '{print $NF}' | sort -u | while read p; do
  killall "$p" 2>/dev/null
done
sleep 4

# ============ 6. статус ============
echo "================ РЕЗУЛЬТАТ ================"
echo "-- либы видны:"
ls -la /vendor/lib64/soundfx/libdolbyaidlshim.so /vendor/lib64/soundfx/libswdap.so /odm/lib64/libutdlb.so 2>&1
echo "-- конфиги:"
grep -n "dap_shim\|dap_mod" /vendor/etc/audio_effects_config.xml /odm/etc/audio_effects_config.xml 2>/dev/null
echo "-- лог фабрики эффектов:"
logcat -d -t 300 2>/dev/null | grep -iE "EffectFactory|EffectsFactory|libdolbyaidlshim|libswdap|DolbyAidlShim|AELI" | tail -40
echo "==========================================="
echo "Тест звука! Откат: sh /data/local/tmp/dolby/deploy2.sh undo"
