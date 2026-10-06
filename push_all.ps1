# Dolby Atmos - залить на телефон и включить (одной командой)
# Запуск:  правой кнопкой по файлу -> "Выполнить с помощью PowerShell"
#     или: powershell -ExecutionPolicy Bypass -File push_all.ps1
#
# Что делает:
#   1) копирует на телефон свежий мост (libdolbyaidlshim.so), скрипты и приложение
#   2) запускает go.sh: маунты + мост + перезапуск аудио
#   3) ставит приложение Dolby Atmos и показывает хвост лога

$ErrorActionPreference = "Continue"
$adb = if ($env:ADB) { $env:ADB } else { "adb" }
$root = $PSScriptRoot

if (-not (Test-Path $adb)) {
    # запасные пути
    $cands = @(
        "C:\Users\1\Downloads\platform-tools-latest-windows\platform-tools\adb.exe",
        "C:\Users\1\Downloads\scrcpy-win64-v4.1\scrcpy-win64-v4.1\adb.exe",
        "C:\Users\1\Downloads\ADB_AppControl\adb\adb.exe"
    )
    foreach ($c in $cands) { if (Test-Path $c) { $adb = $c; break } }
}

Write-Host "adb: $adb" -ForegroundColor Cyan
& $adb devices

Write-Host "`n[1/4] копирую файлы..." -ForegroundColor Cyan
& $adb shell "mkdir -p /data/local/tmp/dolby"
& $adb push "$root/build\out\libdolbyaidlshim.so" /data/local/tmp/dolby/newshim.so
& $adb push "$root/deploy\go.sh"                  /data/local/tmp/dolby/go.sh
& $adb push "$root/deploy\dolby_params.txt"       /data/local/tmp/dolby/dolby_params.txt
& $adb push "$root/app\DolbyAtmos.apk"            /data/local/tmp/DolbyAtmos.apk

Write-Host "`n[2/4] запускаю Dolby (маунты + мост + перезапуск аудио)..." -ForegroundColor Cyan
& $adb shell "su -c 'sh /data/local/tmp/dolby/go.sh'"

Write-Host "`n[3/4] ставлю приложение..." -ForegroundColor Cyan
& $adb uninstall com.dolby.atmos | Out-Null
& $adb install -r "$root/app\DolbyAtmos.apk"

Write-Host "`n[4/4] готово!" -ForegroundColor Green
Write-Host "Включи музыку и смотри лог:"
Write-Host "  & `"$adb`" shell `"su -c 'tail -30 /data/local/tmp/dolby_shim.log'`""
Write-Host ""
Write-Host "Ожидаемые строки: SET_VALUES: 35 параметров ... r=0 status=0"
Write-Host "и в конце: DSP check: in0=... out0=... maxdiff=...  (maxdiff > 0 = Dolby работает)"
Write-Host ""
Write-Host "Выключить/включить на лету: приложение Dolby Atmos (тумблер) или"
Write-Host "  & `"$adb`" shell `"su -c 'echo 1 > /data/vendor/dolby/dolby_bypass'`"   # выкл"
Write-Host "  & `"$adb`" shell `"su -c 'rm -f /data/vendor/dolby/dolby_bypass'`"        # вкл"
