#!/usr/bin/env bash
# Builds Cha-Cha for Linux (docs/linux.md): on a Linux machine (Arch, Ubuntu / Debian, Fedora...), in a distrobox on
# SteamOS, or from WSL2 on the Windows checkout.
#
# Usage: scripts/build-linux.sh [--rpx FILE | --wua FILE] [--tests] [build dir]
#   (default ~/cha-cha-build, or ~/cha-cha-build-<checkout folder> when that one is another checkout's)
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
given=""
rpx="" wua="" tests=0
while [ $# -gt 0 ]; do
    case "$1" in
        --rpx) rpx="${2:?--rpx needs a file}"; shift 2 ;;
        --wua) wua="${2:?--wua needs a file}"; shift 2 ;;
        --tests) tests=1; shift ;;
        -h|--help) sed -n '/^set -euo pipefail/q;2,$p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "unknown option $1 (see --help)" >&2; exit 2 ;;
        *) given="$1"; shift ;;
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
. "$src/scripts/lib/game-exe.sh"
mh3u_place_executable

# ---- build folder --------------------------------------------------------------------------------------------------
. "$src/scripts/lib/build-dir.sh"
mh3u_build_dir "$HOME/cha-cha-build" "$given" ".:$src"

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
