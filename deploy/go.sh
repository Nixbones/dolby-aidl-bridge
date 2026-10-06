#!/system/bin/sh
# =============================================================================
# Dolby Atmos - полный запуск после перезагрузки (root уже должен быть)
#   1) маунты (deploy2.sh), 2) свежий мост, 3) файл-подстройка,
#   4) перезапуск аудио, 5) показать хвост лога
# Запуск:  su -c "sh /data/local/tmp/dolby/go.sh"
# Откат:   su -c "sh /data/local/tmp/dolby/deploy2.sh undo"   (или перезагрузка)
# =============================================================================
BASE=/data/adb/dolby
TMP=/data/local/tmp/dolby

log() { echo "[go] $*"; }

# --- 1. маунты + конфиги -----------------------------------------------------
if ! mount | grep -q "on /vendor/lib64/soundfx "; then
  sh $TMP/deploy2.sh go || { log "deploy2.sh не отработал"; exit 1; }
else
  log "маунты уже есть"
fi

# --- 2. свежий мост + служебные либы ----------------------------------------
[ -f $TMP/newshim.so ] && sh $TMP/shimsync2.sh

# --- 3. файл-подстройка (если есть) -----------------------------------------
mkdir -p /data/vendor/dolby
if [ -f $TMP/dolby_params.txt ]; then
  cp -f $TMP/dolby_params.txt /data/vendor/dolby/dolby_params.txt
  log "подстройка: $(grep -c -v '^#' $TMP/dolby_params.txt) строк"
fi

# --- 3b. права: приложение должно писать эти файлы БЕЗ запроса root ---------
chmod 777 /data/vendor/dolby 2>/dev/null
[ -f /data/vendor/dolby/dolby_params.txt ] || : > /data/vendor/dolby/dolby_params.txt
chmod 666 /data/vendor/dolby/dolby_bypass /data/vendor/dolby/dolby_params.txt           /data/local/tmp/dolby_params.txt 2>/dev/null
log "права на файлы управления выставлены (им сможет управлять приложение)"

# --- 4. перезапуск аудио ----------------------------------------------------
rm -f /data/vendor/dolby/dolby_bypass /data/local/tmp/dolby_bypass
: > /data/local/tmp/dolby_shim.log
# подробный лог Dolby (видно каждый параметр, который принял движок)
setprop persist.vendor.dolby.loglevel 4 2>/dev/null
log "перезапускаю audioserver..."
killall audioserver 2>/dev/null
sleep 3

# --- 5. хвост лога -----------------------------------------------------------
log "готово. Хвост лога:"
tail -25 /data/local/tmp/dolby_shim.log 2>/dev/null
log "теперь включи музыку и смотри:  su -c 'tail -40 /data/local/tmp/dolby_shim.log'"
log "сравнить звук: тумблер в приложении Dolby Atmos (или создать файл"
log "  /data/vendor/dolby/dolby_bypass со строкой 1  = эффект выключен)"
