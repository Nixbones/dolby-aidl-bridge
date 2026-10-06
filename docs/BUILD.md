# Сборка и развёртывание

Документ описывает полный путь: от подготовки окружения до работающего звука на устройстве.

## 1. Что нужно подготовить

| Компонент | Зачем | Где взять |
|---|---|---|
| Android NDK r30 и новее | сборка моста (clang, arm64) | developer.android.com/ndk |
| Build-tools r34 (`aidl.exe`, `aapt2`, `d8`, `apksigner`, `zipalign`) | генерация AIDL-биндингов и сборка приложения | developer.android.com/tools |
| Исходники AOSP (репозитории `hardware/interfaces`, `system/hardware/interfaces`, `system/media`, `system/libfmq`, `frameworks/native`, `system/core`) | заголовки для сборки моста | android.googlesource.com |
| JDK 17 | сборка приложения | adoptium.net |
| Python 3 | вспомогательные скрипты | python.org |
| Библиотеки Dolby под arm64 | сам движок обработки | из прошивки устройства, где Atmos присутствует |

Библиотеки Dolby в репозиторий не входят (см. `LICENSE`). Нужны: `libswdap.so`, `libswgamedap.so`,
`libswvqe.so`, `libdlbvol.so` и служебные `libdlbdsservice.so`, `libdapparamstorage.so`,
`libdlbpreg.so`, `libsqlite.so`, а также переименованные подсистемы с уникальными именами:
`libutdlb.so`, `libhidldlbs.so`, `libstagefright_fdtn_dolby.so`. Переименование зависимостей
делается утилитой `tools/patch_elf_needed.py` (замена имён равной длины в секции динамических
зависимостей), чтобы библиотеки Dolby не конфликтовали с системными `libutils`, `libhidlbase`,
`libstagefright_foundation` на новой версии Android.

## 2. Генерация AIDL-биндингов

Мост использует интерфейсы AIDL-аудио. Биндинги генерируются `aidl.exe` под NDK:

```
aidl --lang=ndk --structured --stability=vintf --min_sdk_version=34 --version=N \
     -h gen/hdr -I <каталоги с frozen-версиями> <список .aidl>
```

Нужны интерфейсы: `android.hardware.audio.effect` (frozen версия 2), `android.hardware.audio.common`
(версия 3), `android.media.audio.common.types` (версия 3), `android.hardware.common` (версия 2) и
`android.hardware.common.fmq` (версия 1). Готовые сгенерированные файлы попадают в `build/gen`
(в репозиторий не входят, так как генерируются из исходников AOSP).

## 3. Дерево заголовков

Сборка линкуется только с LLNDK, но заголовки берутся из AOSP. Ожидаемое дерево внутри `build/`:

```
gen/            сгенерированные AIDL-биндинги (см. пункт 2)
aidl_src/       исходники effect-impl, common и default/include из hardware/interfaces
binder_ndk/     framework/native libs/binder/ndk (include_ndk, include_cpp, include_platform)
binder_plat/    platform/include от binder
libbase_inc/    заглушки для libbase и utils (Errors.h, Trace.h, SystemClock.h, Log.h)
libbase_full/   заголовки libbase
libcutils/      заголовки libcutils
liblog/         заголовки liblog
fmq_full/       заголовки libfmq (include и base)
utils_inc/      заглушки для libutils
stub/           пустые заглушки библиотек для линковки
```

Часть заголовков в AOSP имеет зависимости от внутренних компонентов; в сборке использованы
минимальные локальные заглушки, чтобы не тянуть их целиком.

## 4. Сборка моста

```
cd build
NDK=/путь/к/android-ndk-r30 bash build_shim.sh
```

Результат: `build/out/libdolbyaidlshim.so` (AArch64, линковка с `libbinder_ndk`, `liblog`,
`libcutils`, статический `libc++`).

Сборка состоит из трёх шагов: компиляция сгенерированных AIDL-биндингов, компиляция
`shim/effect_shim.cpp`, линковка.

Проверить, что библиотека загружается, можно пробником `dtest` (исходник в `build/dtest.cpp`):
он делает `dlopen` моста и библиотеки Dolby и печатает адреса точек входа.

## 5. Сборка приложения

```
cd app
BUILD_TOOLS=/путь/к/build-tools/34.0.0 \
ANDROID_JAR=/путь/к/android-all.jar \
bash build_app.sh
```

`android-all.jar` нужен с классами `android.webkit` (в урезанном `android.jar` из состава
build-tools их нет). Скрипт проходит путь `aapt2 -> javac -> d8 -> перепаковка zip -> zipalign ->
подпись` и на выходе даёт `DolbyAtmos.apk`.

## 6. Развёртывание на устройстве

1. Получить root на устройстве (в нашем случае использовался эксплойт, о котором отдельный
   репозиторий; подойдёт и обычный root).
2. Скопировать на устройство каталог с библиотеками Dolby (структура `vendor/lib64` и
   `vendor/lib64/soundfx`) и скрипты из `deploy/`.

```
adb push payload /data/local/tmp/dolby/payload
adb push deploy/*.sh /data/local/tmp/dolby/
adb shell "su -c 'sh /data/local/tmp/dolby/go.sh'"
```

`go.sh` последовательно: монтирует каталоги библиотек и патчит конфиги (`deploy2.sh`), копирует
свежий мост (`shimsync2.sh`), выставляет права на файлы управления, перезапускает аудио-сервисы.

3. Установить приложение:

```
adb install -r app/DolbyAtmos.apk
```

4. Включить музыку и проверить лог моста:

```
adb shell "su -c 'tail -40 /data/local/tmp/dolby_shim.log'"
```

Ожидаемые строки: `SET_VALUES: N параметров ... r=0 status=0` (параметры приняты) и
`DSP: ... diff=4096/4096` (обработка реально меняет сигнал).

Откат: `sh /data/local/tmp/dolby/deploy2.sh undo` или перезагрузка.

## 7. Если что-то не работает

| Симптом | Что проверить |
|---|---|
| В логе системы эффектов по-прежнему 20, а не 21 | конфиг примонтирован поверх старого файла: нужен `umount` + `mount -o bind` (`refix.sh`, `fixbind.sh`) |
| У эффекта нет записи `type=` | без атрибута `type` парсер пропускает эффект молча |
| Мост не загружается, `dlopen failed: cannot locate symbol` | проверьте, что рядом лежат переименованные библиотеки (`libutdlb.so` и другие) и что `LD_LIBRARY_PATH` узкий: только каталог с библиотеками Dolby и `/system/lib64` |
| `SET_CONFIG` возвращает `-22` | неверный формат сэмплов: должен быть `5` (`PCM_FLOAT`) |
| `process` возвращает `-38` | не отправлена `EFFECT_CMD_INIT` |
| Параметры принимаются, но звук не меняется | в посылке `activeDevice` и `deviceId` должны быть реальными (не нули), см. `docs/TECHNICAL.md` |
| Замер показывает нулевую разницу | Dolby обрабатывает буфер на месте: сравнение нужно делать со снимком входа до обработки |
