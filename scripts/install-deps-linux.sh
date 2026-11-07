#!/usr/bin/env bash
# Installs what scripts/build-linux.sh and scripts/package-appimage.sh need (docs/linux.md), with the distribution's
# package manager: Arch / Manjaro / EndeavourOS (pacman), Debian / Ubuntu (apt), Fedora (dnf).
# --android: also what scripts/build-android.sh needs (a JDK 17, unzip, make; the Android SDK / NDK it fetches itself).
# SteamOS keeps its system read-only: build in a distrobox instead (Ubuntu 24.04 or Arch), e.g.
#   distrobox create -n chacha -i ubuntu:24.04 && distrobox enter chacha
# then run this script inside it.
set -euo pipefail
have() { command -v "$1" >/dev/null 2>&1; }
. /etc/os-release 2>/dev/null || true
if [ "${ID:-}" = "steamos" ]; then
    echo "SteamOS: build in a distrobox (see the top of this script), then run this script inside it." >&2
    exit 1
fi
sudo=""; [ "$(id -u)" -ne 0 ] && sudo="sudo"
android=0
[ "${1:-}" = "--android" ] && android=1

if have pacman; then
    $sudo pacman -S --needed --noconfirm base-devel git clang lld cmake ninja pkgconf python file curl zstd \
        vulkan-headers vulkan-icd-loader shaderc ffmpeg \
        libx11 libxext libxrandr libxcursor libxi libxss libxtst libxkbcommon wayland wayland-protocols mesa \
        libdecor alsa-lib libpulse
    [ "$android" = 0 ] || $sudo pacman -S --needed --noconfirm jdk17-openjdk unzip make
elif have apt-get; then
    $sudo apt-get update
    $sudo apt-get install -y build-essential git clang lld cmake ninja-build pkg-config python3 file curl libzstd-dev \
        libvulkan-dev libshaderc-dev libavcodec-dev libavutil-dev \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
        libwayland-dev wayland-protocols libegl-dev libdrm-dev libgbm-dev libdecor-0-dev libasound2-dev libpulse-dev
    [ "$android" = 0 ] || $sudo apt-get install -y openjdk-17-jdk-headless unzip make
elif have dnf; then
    $sudo dnf install -y gcc-c++ git clang lld cmake ninja-build pkgconf-pkg-config python3 file curl libzstd-devel \
        vulkan-headers vulkan-loader-devel libshaderc-devel ffmpeg-free-devel \
        libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel libXScrnSaver-devel libXtst-devel \
        libxkbcommon-devel wayland-devel wayland-protocols-devel mesa-libEGL-devel libdrm-devel mesa-libgbm-devel \
        libdecor-devel alsa-lib-devel pulseaudio-libs-devel
    [ "$android" = 0 ] || $sudo dnf install -y java-17-openjdk-devel unzip make
else
    echo "unknown distribution: install clang, lld, CMake 3.25+, Ninja, pkg-config, the Vulkan headers and loader," >&2
    echo "shaderc, FFmpeg (libavcodec), the SDL3 build dependencies, python3, file and curl yourself" >&2
    exit 1
fi

ver="$(cmake --version | head -1 | awk '{print $3}')"
if [ "$(printf '%s\n3.25\n' "$ver" | sort -V | head -1)" != "3.25" ]; then
    echo "note: CMake $ver is older than 3.25: install a newer one (pip install --user cmake, or Kitware's)" >&2
fi
if [ "$android" = 1 ] && have javac; then
    jv="$(javac -version 2>&1 | grep -E '^javac' | grep -oE '[0-9]+' | head -1)"
    [ "${jv:-0}" -ge 17 ] || echo "note: javac is version $jv; pick JDK 17+ (Arch: sudo archlinux-java set java-17-openjdk)" >&2
fi
if [ "$android" = 1 ]; then
    echo "done. Next: scripts/build-android.sh --wua <your dump.wua>   (or --rpx <MH3G_Cafe_US.rpx>)"
else
    echo "done. Next: scripts/build-linux.sh --wua <your dump.wua>   (or --rpx <MH3G_Cafe_US.rpx>)"
fi
