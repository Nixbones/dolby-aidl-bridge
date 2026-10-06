#!/bin/bash
# Сборка APK "Dolby Atmos" без Android Studio:
#   aapt2 link (манифест+assets) -> javac -> d8 -> перепаковка zip -> zipalign -> apksigner
set -e
APP="$(cd "$(dirname "$0")" && pwd)"          # work/dolby/app
BT="${BUILD_TOOLS:-/path/to/android-sdk/build-tools/34.0.0}"
JAR="${ANDROID_JAR:-/path/to/android-all.jar}"   # android.jar с android.webkit (в голом SDK его нет)
D8=$BT/lib/d8.jar
SIGN=$BT/lib/apksigner.jar
KS="${KEYSTORE:-$HOME/.android/debug.keystore}"
JAVAC="javac"; JAVA="java"

cd "$APP"
rm -rf classes dex app.unsigned.apk app.zip app-aligned.apk DolbyAtmos.apk
mkdir -p classes dex

echo "=== 1/6 aapt2 link ==="
"$BT/aapt2.exe" link -o app.unsigned.apk \
  --manifest AndroidManifest.xml -A assets \
  -I "$JAR" --min-sdk-version 29 --target-sdk-version 35 \
  --version-code 2 --version-name 2.0

echo "=== 2/6 javac ==="
"$JAVAC" -source 8 -target 8 -nowarn -cp "$JAR" -d classes \
  $(find src -name "*.java")

echo "=== 3/6 d8 ==="
"$JAVA" -cp "$D8" com.android.tools.r8.D8 --min-api 29 --output dex \
  $(find classes -name "*.class")

echo "=== 4/6 перепаковка zip (пути с прямыми слэшами) ==="
python - <<'PY'
import zipfile, os
src = zipfile.ZipFile('app.unsigned.apk')
out = zipfile.ZipFile('app.zip', 'w', zipfile.ZIP_DEFLATED)
for it in src.infolist():
    name = it.filename.replace('\\', '/')
    data = src.read(it.filename)
    if name in ('AndroidManifest.xml', 'resources.arsc') or name.endswith('/'):
        out.writestr(zipfile.ZipInfo(name), data, zipfile.ZIP_STORED)
    else:
        out.writestr(name, data)
src.close()
out.close()
# assets из дерева (если aapt2 не забрал) - на всякий случай проверяем
have = set(zipfile.ZipFile('app.zip').namelist())
print("assets в apk:", sum(1 for n in have if n.startswith('assets/')))
PY

echo "=== 5/6 добавляем classes.dex + zipalign ==="
python - <<'PY'
import zipfile
dex = zipfile.ZipFile('dex/classes.dex') if False else None
out = zipfile.ZipFile('app.zip', 'a', zipfile.ZIP_DEFLATED)
out.write('dex/classes.dex', 'classes.dex')
out.close()
PY
"$BT/zipalign.exe" -p -f 4 app.zip app-aligned.apk

echo "=== 6/6 подпись ==="
"$JAVA" -jar "$SIGN" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android \
  --ks-key-alias androiddebugkey --out DolbyAtmos.apk app-aligned.apk
"$JAVA" -jar "$SIGN" verify --print-certs DolbyAtmos.apk | head -5
ls -la DolbyAtmos.apk
echo "ГОТОВО: $APP/DolbyAtmos.apk"
