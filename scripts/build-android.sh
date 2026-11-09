#!/usr/bin/env bash
# Builds Cha-Cha for Android (arm64) as an APK (docs/android.md), on Linux (Arch, Ubuntu / Debian, Fedora...).
#
# Usage: scripts/build-android.sh [--rpx FILE | --wua FILE] [--install] [build dir]
#   (default ~/cha-cha-android-build, or ~/cha-cha-android-build-<checkout folder> when that one is another checkout's)
#   --rpx FILE   your US v32 executable (MH3G_Cafe_US.rpx from the update, title 0005000E10118300)
#   --wua FILE   your .wua dump: the executable is taken out of it
#   --install    install the APK on the phone connected over USB (adb, USB debugging on)
# The build translates that executable into the app, so it is needed here; the rest of the game is chosen on the
# phone. As for Linux, only this one file is needed, and later builds find it in extracted/ by themselves.
#
# Needs: a JDK 17+ (javac, keytool), python3, git, curl, unzip, CMake 3.25+, Ninja, a host C++ compiler
# (scripts/install-deps-linux.sh --android installs them). The Android SDK and NDK go to $ANDROID_HOME (default
# ~/Android/Sdk) on first use: the command-line tools are downloaded, then sdkmanager installs the platform, build
# tools and the newest NDK (r28 or newer is needed). shaderc and FFmpeg (H.264 only) are built for Android from source
# into ~/.cache/cha-cha-tools/android. JOBS limits the parallel compilers (about 2 GB of memory each).
# Environment: ANDROID_HOME, ANDROID_NDK_HOME (a specific NDK), APP_ID (default org.chacha.mh3u), JOBS.
set -euo pipefail
src="$(cd "$(dirname "$0")/.." && pwd)"
given=""
rpx="" wua="" install=0
while [ $# -gt 0 ]; do
    case "$1" in
        --rpx) rpx="${2:?--rpx needs a file}"; shift 2 ;;
        --wua) wua="${2:?--wua needs a file}"; shift 2 ;;
        --install) install=1; shift ;;
        -h|--help) sed -n '/^set -euo pipefail/q;2,$p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "unknown option $1 (see --help)" >&2; exit 2 ;;
        *) given="$1"; shift ;;
    esac
done
die() { echo "build-android: $*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }
step() { echo; echo "== $*"; }

api=29                      # minSdk: Android 10 (Vulkan 1.1 on every device)
target_api=35               # targetSdk / compileSdk
build_tools=35.0.0
app_id="${APP_ID:-org.chacha.mh3u}"
shaderc_tag=v2026.3
ffmpeg_tag=n7.1
cmdline_tools_zip="${CMDLINE_TOOLS_URL:-https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip}"
cache="$HOME/.cache/cha-cha-tools/android"
. "$src/scripts/lib/build-dir.sh"
mh3u_build_dir "$HOME/cha-cha-android-build" "$given" "android:$src" "host-tools:$src/recomp"
mkdir -p "$cache" "$build"

# ---- tools ---------------------------------------------------------------------------------------------------------
[ -d /opt/cmake/bin ] && export PATH="/opt/cmake/bin:$PATH"
for t in javac jar java keytool python3 git curl unzip cmake ninja make; do
    have "$t" || die "$t is missing - run scripts/install-deps-linux.sh --android"
done
java_major="$(javac -version 2>&1 | grep -E '^javac' | grep -oE '[0-9]+' | head -1)"
[ "${java_major:-0}" -ge 17 ] || die "JDK 17 or newer is needed (javac -version says $(javac -version 2>&1))"
cmake_ver="$(cmake --version | head -1 | awk '{print $3}')"
[ "$(printf '%s\n3.25\n' "$cmake_ver" | sort -V | head -1)" = "3.25" ] || die "CMake $cmake_ver is too old (3.25+ needed)"

# ---- the game's executable -----------------------------------------------------------------------------------------
step "game executable"
. "$src/scripts/lib/game-exe.sh"
mh3u_place_executable

# ---- Android SDK and NDK -------------------------------------------------------------------------------------------
step "Android SDK and NDK"
export ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
sdkmanager="$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager"
if [ ! -x "$sdkmanager" ]; then
    echo "downloading the Android command-line tools into $ANDROID_HOME"
    tmp="$(mktemp -d)"
    curl -fL --progress-bar -o "$tmp/tools.zip" "$cmdline_tools_zip"
    unzip -q "$tmp/tools.zip" -d "$tmp"
    mkdir -p "$ANDROID_HOME/cmdline-tools"
    rm -rf "$ANDROID_HOME/cmdline-tools/latest"
    mv "$tmp/cmdline-tools" "$ANDROID_HOME/cmdline-tools/latest"
    rm -rf "$tmp"
fi
sdk() { "$sdkmanager" --sdk_root="$ANDROID_HOME" "$@"; }
yes 2>/dev/null | sdk --licenses >/dev/null || true
ndk="${ANDROID_NDK_HOME:-}"
if [ -z "$ndk" ]; then  # the newest NDK installed, else the newest stable one sdkmanager offers
    ndk="$(ls -d "$ANDROID_HOME"/ndk/[0-9]* 2>/dev/null | sort -V | tail -1 || true)"
    ndk_major="$(basename "${ndk:-0}" | cut -d. -f1)"
    if [ -z "$ndk" ] || [ "$ndk_major" -lt 28 ]; then
        ndk_ver="$(sdk --list 2>/dev/null | grep -oE 'ndk;[0-9]+\.[0-9]+\.[0-9]+([[:space:]]|$)' | tr -d ' \t' | cut -d';' -f2 | sort -V | tail -1)"
        [ -n "$ndk_ver" ] || die "sdkmanager lists no NDK"
        sdk "ndk;$ndk_ver" >/dev/null
        ndk="$ANDROID_HOME/ndk/$ndk_ver"
    fi
fi
ndk_major="$(basename "$ndk" | cut -d. -f1)"
if [ -f "$ndk/source.properties" ]; then ndk_major="$(grep -oE 'Pkg.Revision *= *[0-9]+' "$ndk/source.properties" | grep -oE '[0-9]+$')"; fi
[ "${ndk_major:-0}" -ge 28 ] || die "NDK r28 or newer is needed ($ndk); the code uses C++20's std::atomic_ref (libc++ 19)"
[ -d "$ANDROID_HOME/platforms/android-$target_api" ] || sdk "platforms;android-$target_api" >/dev/null
[ -d "$ANDROID_HOME/build-tools/$build_tools" ] || sdk "build-tools;$build_tools" >/dev/null
[ "$install" = 0 ] || [ -x "$ANDROID_HOME/platform-tools/adb" ] || sdk "platform-tools" >/dev/null
bt="$ANDROID_HOME/build-tools/$build_tools"
android_jar="$ANDROID_HOME/platforms/android-$target_api/android.jar"
tc="$ndk/toolchains/llvm/prebuilt/linux-x86_64"
for f in "$android_jar" "$bt/aapt2" "$bt/d8" "$bt/zipalign" "$bt/apksigner" "$tc/bin/clang" "$ndk/build/cmake/android.toolchain.cmake"; do
    [ -e "$f" ] || die "missing $f (the SDK install did not finish?)"
done
echo "NDK: $ndk"

# ---- shaderc and FFmpeg for Android --------------------------------------------------------------------------------
step "shaderc ($shaderc_tag) source"
shaderc="$cache/shaderc-$shaderc_tag"
if [ ! -f "$shaderc/.synced" ]; then
    rm -rf "$shaderc"
    git clone -q --depth 1 --branch "$shaderc_tag" https://github.com/google/shaderc.git "$shaderc"
    (cd "$shaderc" && python3 utils/git-sync-deps >/dev/null)  # glslang, SPIRV-Tools, SPIRV-Headers at shaderc's revisions
    touch "$shaderc/.synced"
fi

step "FFmpeg ($ffmpeg_tag, H.264 decoder only) for Android"
ffmpeg="$cache/ffmpeg-$ffmpeg_tag-api$api-ndk$ndk_major"
if [ ! -f "$ffmpeg/lib/libavcodec.a" ]; then
    ffsrc="$cache/ffmpeg-src-$ffmpeg_tag"
    [ -d "$ffsrc" ] || git clone -q --depth 1 --branch "$ffmpeg_tag" https://github.com/FFmpeg/FFmpeg.git "$ffsrc"
    (
        cd "$ffsrc"
        make distclean >/dev/null 2>&1 || true
        ./configure --prefix="$ffmpeg" --target-os=android --arch=aarch64 --cpu=armv8-a --enable-cross-compile \
            --cc="$tc/bin/aarch64-linux-android$api-clang" --cxx="$tc/bin/aarch64-linux-android$api-clang++" \
            --ar="$tc/bin/llvm-ar" --nm="$tc/bin/llvm-nm" --ranlib="$tc/bin/llvm-ranlib" --strip="$tc/bin/llvm-strip" \
            --sysroot="$tc/sysroot" --enable-pic --enable-static --disable-shared --disable-programs --disable-doc \
            --disable-everything --enable-decoder=h264 --enable-parser=h264 --disable-avdevice --disable-avformat \
            --disable-swresample --disable-swscale --disable-avfilter --disable-network --disable-autodetect >/dev/null
        make -j"$(nproc)" >/dev/null
        make install >/dev/null
    )
fi

# ---- host tools (the recompiler runs on this machine) -------------------------------------------------------------
step "host tools"
cmake -S "$src/recomp" -B "$build/host-tools" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$build/host-tools" --target recomp_emit emit_selftest_gen

# ---- the native library -------------------------------------------------------------------------------------------
step "libmain.so"
if [ -z "${JOBS:-}" ]; then
    mem_gb=$(( $(awk '/MemAvailable/ {print $2}' /proc/meminfo) / 1048576 ))
    JOBS=$(( mem_gb / 2 )); [ "$JOBS" -lt 1 ] && JOBS=1
    [ "$JOBS" -gt "$(nproc)" ] && JOBS="$(nproc)"
fi
env -u VULKAN_SDK cmake -S "$src" -B "$build/android" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM="android-$api" -DANDROID_STL=c++_static \
    -DMH3U_HOST_TOOLS="$build/host-tools" -DMH3U_RECOMP_OUT="$build/recomp-out" \
    -DMH3U_SHADERC_SOURCE="$shaderc" -DMH3U_FFMPEG_DIR="$ffmpeg"
echo "building with $JOBS parallel jobs"
cmake --build "$build/android" --target mh3u -j "$JOBS"
libmain="$build/android/runtime/boot/libmain.so"
libsdl="$(find "$build/android/_deps" -name 'libSDL3.so' -print -quit)"
[ -f "$libmain" ] && [ -n "$libsdl" ] || die "the build did not produce libmain.so / libSDL3.so"

# ---- the APK -------------------------------------------------------------------------------------------------------
step "APK"
apk="$build/apk"
rm -rf "$apk"
mkdir -p "$apk/classes" "$apk/dex" "$apk/compiled" "$apk/res" "$apk/lib/arm64-v8a"
version="$(git -C "$src" describe --always --dirty 2>/dev/null || echo dev)"
version_code=$(( $(date +%s) / 60 ))  # rises with every build, so an install replaces the last one
sed -e "s/@APP_ID@/$app_id/" -e "s/@VERSION_CODE@/$version_code/" -e "s/@VERSION_NAME@/$version/" \
    "$src/android/AndroidManifest.xml" > "$apk/AndroidManifest.xml"
# resources: ours, and the launcher icon from icon/icon.ico (its largest PNG image)
cp -r "$src/android/res/." "$apk/res/"
mkdir -p "$apk/res/mipmap-xxxhdpi"
python3 - "$src/icon/icon.ico" "$apk/res/mipmap-xxxhdpi/ic_launcher.png" <<'PY'
import struct, sys
ico = open(sys.argv[1], 'rb').read()
best = None
for i in range(struct.unpack_from('<H', ico, 4)[0]):
    w, h, _, _, _, _, size, offset = struct.unpack_from('<BBBBHHII', ico, 6 + 16 * i)
    data = ico[offset:offset + size]
    if data.startswith(b'\x89PNG') and (best is None or (w or 256) * (h or 256) > best[0]):
        best = ((w or 256) * (h or 256), data)
if best is None:
    sys.exit('icon.ico has no PNG image')
open(sys.argv[2], 'wb').write(best[1])
PY
find "$apk/res" -type f -print0 | while IFS= read -r -d '' f; do "$bt/aapt2" compile "$f" -o "$apk/compiled/"; done
"$bt/aapt2" link -o "$apk/base.apk" -I "$android_jar" --manifest "$apk/AndroidManifest.xml" \
    --min-sdk-version "$api" --target-sdk-version "$target_api" --auto-add-overlay "$apk"/compiled/*.flat
# Java: SDL3's SDLActivity (from the SDL source the native build fetched) and ChaChaActivity
sdl_java="$(dirname "$(dirname "$libsdl")")/sdl3-src/android-project/app/src/main/java"
[ -d "$sdl_java" ] || sdl_java="$build/android/_deps/sdl3-src/android-project/app/src/main/java"
[ -d "$sdl_java" ] || die "SDL's Java sources are missing ($sdl_java)"
find "$sdl_java" "$src/android/java" -name '*.java' > "$apk/sources.txt"
javac -encoding UTF-8 -source 8 -target 8 -bootclasspath "$android_jar" -nowarn -Xlint:-options -d "$apk/classes" @"$apk/sources.txt"
jar cf "$apk/classes.jar" -C "$apk/classes" .
"$bt/d8" --release --min-api "$api" --lib "$android_jar" --output "$apk/dex" "$apk/classes.jar"
# native libraries: stripped copies (keep $libmain for symbolising crash logs)
"$tc/bin/llvm-strip" --strip-unneeded -o "$apk/lib/arm64-v8a/libmain.so" "$libmain"
"$tc/bin/llvm-strip" --strip-unneeded -o "$apk/lib/arm64-v8a/libSDL3.so" "$libsdl"
python3 - "$apk/base.apk" "$apk" <<'PY'
import os, sys, zipfile
apk, root = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(apk, 'a', zipfile.ZIP_DEFLATED) as z:
    z.write(os.path.join(root, 'dex', 'classes.dex'), 'classes.dex')
    for name in sorted(os.listdir(os.path.join(root, 'lib', 'arm64-v8a'))):
        z.write(os.path.join(root, 'lib', 'arm64-v8a', name), 'lib/arm64-v8a/' + name)
PY
"$bt/zipalign" -f 4 "$apk/base.apk" "$apk/aligned.apk"
# signed with a key of this machine's own (kept, so later builds install over this one)
ks="$cache/debug.keystore"
[ -f "$ks" ] || keytool -genkeypair -keystore "$ks" -alias chacha -keyalg RSA -keysize 2048 -validity 10000 \
    -storepass android -keypass android -dname "CN=Cha-Cha local build" >/dev/null 2>&1
mkdir -p "$src/build/release"
out="$src/build/release/Cha-Cha-$version-arm64.apk"
"$bt/apksigner" sign --ks "$ks" --ks-key-alias chacha --ks-pass pass:android --key-pass pass:android --out "$out" "$apk/aligned.apk"
ls -l "$out"

if [ "$install" = 1 ]; then
    step "installing on the phone"
    "$ANDROID_HOME/platform-tools/adb" install -r "$out"
fi
echo
echo "APK: $out"
echo "Copy it to the phone and open it to install (allow installs from that app), or use --install over USB."
