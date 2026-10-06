#!/bin/bash
# Сборка libdolbyaidlshim.so (AArch64)
set -e
# путь к NDK (clang для Android). Переопределяется: NDK=/путь bash build_shim.sh
B="${NDK:-/c/ndk-r30/android-ndk-r30}/toolchains/llvm/prebuilt/windows-x86_64/bin"
CLANG="$B/aarch64-linux-android35-clang++.cmd"
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

INC="-I gen/hdr \
 -I binder_ndk \
 -I binder_plat -I libbase_inc \
 -I fmq_full/include -I fmq_full/base -I libbase_full -I libcutils/include -I liblog/include -I utils_inc \
 -I aidl_src/efimpl/effect-impl \
 -I aidl_src/aidl/common/include \
 -I aidl_src/aidl/default/include"

CXXFLAGS="-std=c++17 -O2 -fPIC -shared -ffast-math -fvisibility=hidden \
  -D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS=0 -DLOG_TAG=\"\\\"DolbyAidlShim\\\"\" \
  -Wno-thread-safety-analysis -Wno-thread-safety-attributes -Wno-unknown-attributes \
  -Wno-unguarded-availability-new"

# 1) сгенерированные aidl-байндинги
SRCS=$(find gen/src -name "*.cpp" | sed 's/^/\x27/;s/$/\x27/' | tr '\n' ' ')
echo "=== компиляция gen/src ($(find gen/src -name '*.cpp' | wc -l) файлов)..."
mkdir -p out
for f in $(find gen/src -name "*.cpp"); do
  o="out/$(echo $f | sed 's|/|_|g').o"
  [ -f "$o" ] || "$CLANG" -std=c++17 -O1 -fPIC $INC -Wno-thread-safety-analysis -c "$f" -o "$o" 2>> compile_errors.log || { echo "FAIL: $f"; tail -20 compile_errors.log; exit 1; }
done
echo "gen OK"

# 2) шим
echo "=== компиляция шима..."
"$CLANG" -std=c++17 -O2 -fPIC $INC -Wno-thread-safety-analysis -c ../shim/effect_shim.cpp -o out/effect_shim.o || { echo FAIL; exit 1; }

# 3) линковка
echo "=== линковка..."
"$CLANG" -shared -o out/libdolbyaidlshim.so out/*.o \
  -L stub -lbinder_ndk -llog -lcutils -static-libstdc++ \
  -Wl,-soname,libdolbyaidlshim.so
"$B/llvm-objdump.exe" --private-headers out/libdolbyaidlshim.so 2>/dev/null | grep NEEDED || true
ls -la out/libdolbyaidlshim.so
echo DONE
