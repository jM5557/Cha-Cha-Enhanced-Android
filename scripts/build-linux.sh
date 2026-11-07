#!/usr/bin/env bash
# Builds Cha-Cha for Linux (docs/linux.md): on a Linux machine (Arch, Ubuntu / Debian, Fedora...), in a distrobox on
# SteamOS, or from WSL2 on the Windows checkout.
#
# Usage: scripts/build-linux.sh [--rpx FILE | --wua FILE] [--tests] [build dir]   (default ~/cha-cha-build)
#   --rpx FILE   your US v32 executable (MH3G_Cafe_US.rpx from the update, title 0005000E10118300)
#   --wua FILE   your .wua dump: the executable is taken out of it (ZArchive is built on first use)
#   --tests      also build everything and run the tests
# Without --rpx / --wua the executable must already be at extracted/0005000e10118300_v32/code/MH3G_Cafe_US.elf (a
# decompressed ELF or the .rpx itself under that name). The build translates that executable into the program, so it
# is needed here; the rest of the game is only needed when you run Cha-Cha.
#
# Needs (scripts/install-deps-linux.sh installs them): clang 15+, CMake 3.25+, Ninja, pkg-config, Vulkan headers and
# loader, shaderc (the distribution's, or the LunarG Vulkan SDK in /opt/vulkan/<version>/x86_64 or $VULKAN_SDK),
# FFmpeg (libavcodec), the SDL3 build dependencies. Keep the build dir on a Linux disk (not /mnt/...).
# JOBS limits the parallel compilers (the generated game code needs about 1-2 GB of memory per compiler).
set -euo pipefail
src="$(cd "$(dirname "$0")/.." && pwd)"
build="$HOME/cha-cha-build"
rpx="" wua="" tests=0
while [ $# -gt 0 ]; do
    case "$1" in
        --rpx) rpx="${2:?--rpx needs a file}"; shift 2 ;;
        --wua) wua="${2:?--wua needs a file}"; shift 2 ;;
        --tests) tests=1; shift ;;
        -h|--help) sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "unknown option $1 (see --help)" >&2; exit 2 ;;
        *) build="$1"; shift ;;
    esac
done
die() { echo "build-linux: $*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

# ---- tools ---------------------------------------------------------------------------------------------------------
[ -d /opt/cmake/bin ] && export PATH="/opt/cmake/bin:$PATH"
for t in cmake ninja pkg-config git; do have "$t" || die "$t is missing - run scripts/install-deps-linux.sh"; done
cmake_ver="$(cmake --version | head -1 | awk '{print $3}')"
[ "$(printf '%s\n3.25\n' "$cmake_ver" | sort -V | head -1)" = "3.25" ] || die "CMake $cmake_ver is too old (3.25+ needed)"
if [ -z "${CC:-}" ]; then
    if have clang; then export CC=clang CXX=clang++
    elif have clang-15; then export CC=clang-15 CXX=clang++-15
    else die "clang is missing - run scripts/install-deps-linux.sh"; fi
fi
linker_flags=()
if lld="$(command -v ld.lld || command -v ld.lld-15)"; then
    linker_flags=(-DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=$lld" -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=$lld")
fi
# shaderc: the LunarG SDK when there is one, else the distribution's (tools/shaderc-latte/CMakeLists.txt)
if [ -z "${VULKAN_SDK:-}" ] && ls -d /opt/vulkan/*/x86_64 >/dev/null 2>&1; then
    VULKAN_SDK="$(ls -d /opt/vulkan/*/x86_64 | sort -V | tail -1)"
fi
if [ -n "${VULKAN_SDK:-}" ] && [ -f "$VULKAN_SDK/lib/libshaderc_combined.a" ]; then
    export VULKAN_SDK
    echo "shaderc: the Vulkan SDK in $VULKAN_SDK"
else
    unset VULKAN_SDK
    pkg-config --exists shaderc || die "shaderc is missing - run scripts/install-deps-linux.sh (or install the LunarG Vulkan SDK)"
    echo "shaderc: the system's"
fi
# the SDK ships headers and tools but not the loader: the system's libvulkan
vulkan_lib="$(ls "${VULKAN_SDK:-/nonexistent}"/lib/libvulkan.so 2>/dev/null || ls /usr/lib/x86_64-linux-gnu/libvulkan.so /usr/lib/libvulkan.so /usr/lib64/libvulkan.so 2>/dev/null | head -1 || true)"
[ -n "$vulkan_lib" ] || die "the Vulkan loader (libvulkan.so) is missing - run scripts/install-deps-linux.sh"

# ---- the game's executable -----------------------------------------------------------------------------------------
target_dir="$src/extracted/0005000e10118300_v32/code"
target="$target_dir/MH3G_Cafe_US.elf"
want_sha="$(grep -i '_v32' "$src/config/target.txt" | grep -o 'sha1=[0-9a-f]*' | head -1 | cut -d= -f2)"  # US v32 .rpx
if [ -n "$wua" ]; then
    [ -f "$wua" ] || die "no such file: $wua"
    zarchive="$(command -v zarchive || true)"
    tools="$HOME/.cache/cha-cha-tools"
    if [ -z "$zarchive" ]; then
        zarchive="$tools/zarchive"
        if [ ! -x "$zarchive" ]; then
            echo "building ZArchive (github.com/Exzap/ZArchive) into $tools"
            mkdir -p "$tools"
            rm -rf "$tools/ZArchive"
            git clone -q --depth 1 https://github.com/Exzap/ZArchive.git "$tools/ZArchive"
            cmake -S "$tools/ZArchive" -B "$tools/ZArchive/build" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
            ninja -C "$tools/ZArchive/build" >/dev/null
            cp "$tools/ZArchive/build/zarchive" "$zarchive"
        fi
    fi
    tmp="$(mktemp -d "${TMPDIR:-/tmp}/cha-cha-wua.XXXXXX")"
    trap 'rm -rf "$tmp"' EXIT
    echo "extracting $wua (the whole archive, then only the executable is kept)"
    "$zarchive" "$wua" "$tmp/out" >/dev/null
    rpx="$(find "$tmp/out" -ipath '*0005000e10118300*' -iname 'MH3G_Cafe_US.rpx' | head -1)"
    if [ -z "$rpx" ]; then
        found="$(find "$tmp/out" -iname '*.rpx' | sed "s|$tmp/out/||")"
        die "no US update executable (0005000e10118300.../code/MH3G_Cafe_US.rpx) in the .wua; it has: ${found:-no .rpx}
Cha-Cha needs Monster Hunter 3 Ultimate US with update v32 in the archive."
    fi
fi
if [ -n "$rpx" ]; then
    [ -f "$rpx" ] || die "no such file: $rpx"
    sha="$(sha1sum "$rpx" | cut -d' ' -f1)"
    if [ -n "$want_sha" ] && [ "$sha" != "$want_sha" ] && [ -z "${MH3U_SKIP_SHA_CHECK:-}" ]; then
        die "$rpx is not the US v32 executable (SHA-1 $sha, expected $want_sha from config/target.txt).
The EU (0005000010117200) and JP releases and other updates are not supported. (An already decompressed
.elf of the right executable has another hash: MH3U_SKIP_SHA_CHECK=1 accepts it.)"
    fi
    mkdir -p "$target_dir"
    cp "$rpx" "$target"
    echo "executable: $rpx (US v32) -> extracted/0005000e10118300_v32/code/MH3G_Cafe_US.elf"
fi
[ -f "$target" ] || die "the game's executable is missing at $target
Pass --rpx <MH3G_Cafe_US.rpx> or --wua <your dump.wua> (US game with update v32). Only this one file is needed
to build; the rest of the game is chosen when Cha-Cha starts."

# ---- build ---------------------------------------------------------------------------------------------------------
if [ -z "${JOBS:-}" ]; then  # about 2 GB of memory per compiler, at most one per core
    mem_gb=$(( $(awk '/MemAvailable/ {print $2}' /proc/meminfo) / 1048576 ))
    JOBS=$(( mem_gb / 2 )); [ "$JOBS" -lt 1 ] && JOBS=1
    [ "$JOBS" -gt "$(nproc)" ] && JOBS="$(nproc)"
fi
echo "building into $build with $CC, $JOBS parallel jobs"
cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DMH3U_RECOMP_OUT="$build/recomp-out" \
      -DVulkan_LIBRARY="$vulkan_lib" "${linker_flags[@]}"
if [ "$tests" = 1 ]; then
    cmake --build "$build" -j "$JOBS"
    ctest --test-dir "$build" --output-on-failure
else
    cmake --build "$build" --target mh3u -j "$JOBS"
fi
echo
echo "built: $build/runtime/boot/Cha-Cha"
echo "AppImage: scripts/package-appimage.sh $build"
