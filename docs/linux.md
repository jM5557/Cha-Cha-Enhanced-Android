# Linux build and AppImage

Goal: ship `Cha-Cha-<version>-x86_64.AppImage` next to the Windows zip on GitHub Releases, same model (the binary
contains the recompiled game code, the player brings their own dump). The public GitHub push waits until both
exist (user decision 2026-10-28).

## Building it yourself (2026-11-07)

The short version is in the README (Building from source → Linux and SteamOS):
`scripts/install-deps-linux.sh`, `scripts/build-linux.sh --wua <dump.wua>` (or `--rpx <MH3G_Cafe_US.rpx>`),
`scripts/package-appimage.sh`.

- **Distributions.** `install-deps-linux.sh` knows pacman (Arch and its derivatives), apt (Debian / Ubuntu) and dnf
  (Fedora). SteamOS has a read-only system: build in a distrobox (`distrobox create -n chacha -i ubuntu:24.04`) and
  run the AppImage on SteamOS itself. An AppImage built on Ubuntu 22.04 runs on the most systems (glibc 2.35+); one
  built on 24.04 or a current Arch needs a system at least that new (SteamOS 3.7 is).
- **The executable.** Only the US v32 `MH3G_Cafe_US.rpx` is needed to build (the build translates it); the full game
  is chosen at the first start. `build-linux.sh --wua` builds ZArchive (Exzap/ZArchive) into
  `~/.cache/cha-cha-tools` on first use, unpacks the archive to a temporary folder, keeps the update's executable and
  deletes the rest. `--rpx` copies the file. Both check its SHA-1 against `config/target.txt` (the ELF loader
  decompresses the RPX itself, so no wiiurpxtool is needed); `MH3U_SKIP_SHA_CHECK=1` accepts an already
  decompressed ELF.
- **shaderc.** The LunarG SDK (`/opt/vulkan/<version>/x86_64` or `$VULKAN_SDK`) when present, else the
  distribution's through pkg-config (`tools/shaderc-latte/CMakeLists.txt`). Ubuntu's `libshaderc_combined.a` leaves
  glslang out, so the system's shared `libshaderc` is linked, and linuxdeploy bundles it into the AppImage.
- **Linker / compiler.** `clang` (or `clang-15`) and `ld.lld` when installed, else the default linker; `CC` / `CXX`
  override. `JOBS` defaults to about one compiler per 2 GB of free memory, at most one per core.
- **Packaging** needs `file` (appimagetool stops without it, after the whole AppDir is made), `curl` and `python3`;
  `package-appimage.sh` checks for them first.

## Status (2026-10-28)

- Ported and building in WSL2 Ubuntu 22.04 with clang 15 (`scripts/build-linux.sh`); Windows builds and all 12
  tests unchanged.
- Headless boot from the checkout's `extracted/` dump: 668 shaders translated (25.6 s), the game runs to the title's
  start menu (4,051 frames in 90 s on llvmpipe, WSL's software Vulkan); the write tracking works (945 written pages
  caught).
- Not yet seen: a window (WSLg or a real Linux desktop), sound, a controller.
- Movies through libavcodec (2026-10-29, issue #2): the 0.1.0 AppImage froze on the intro's first frame; fixed with
  two decoders (`docs/runtime.md` § Movies). Headless in WSL the intro plays to its end (3,673 units, one picture
  each), an A press skips to the title and the attract movie loops.
- Save loading crashed (2026-10-29, issue #3: "new game, crash", "continue, crash"): a race in the write tracking left
  a page read-only without its tracked mark, and the next write ended in "guest memory fault ... (uncommitted)". Fixed
  in `guest_memory_posix.inc`; headless in WSL both New Game -> character creation and Continue -> Moga Village run
  300 s without a fault, and a walk into the player's house and back out (a copy of the user's save) runs clean.
- `scripts/package-appimage.sh` builds `Cha-Cha-<version>-x86_64.AppImage` (72 MB) with linuxdeploy (downloaded on
  first use, user OK 2026-10-28); the Vulkan loader is left to the system. The AppImage boots headless to the title
  in WSL (`APPIMAGE_EXTRACT_AND_RUN=1`).
- Size: Ubuntu's libavcodec brings about 50 libraries (x264, x265, VA-API, OpenCL, cairo...). A minimal FFmpeg
  build (H.264 decoder only, static) would cut most of that.

## Already portable

- The recompiled game code (`recomp/out/`, about 384 MB of C++): plain C++20, no MSVC intrinsics, no inline asm.
- Graphics (Vulkan), windows / input / audio / file dialogs (SDL3), Dear ImGui, zstd, the shader translator.
- Guest threads are `std::thread` (no fibers); locks are standard C++.

## What was ported (survey of the tree)

| Area | Windows today | Linux replacement |
|---|---|---|
| Guest memory (`runtime/core/guest_memory.cpp`) | `VirtualAlloc` reserve / commit, `VirtualProtect` | `mmap(PROT_NONE)` reserve, `mprotect` commit |
| Dirty-page tracking for textures | `MEM_WRITE_WATCH` + `GetWriteWatch` | pages read-only + a `SIGSEGV` handler that marks and unprotects them |
| Fault reporter | `AddVectoredExceptionHandler`, `__try` (2 places) | `sigaction(SIGSEGV)`; module + offset via `dladdr` |
| Movies (`runtime/os/h264.cpp`) | Media Foundation H.264 decoder | FFmpeg `libavcodec` (H.264 only), bundled in the AppImage |
| Launcher pictures (`launcher_image.cpp`) | WIC | SDL3's `SDL_LoadPNG` (the pictures are PNG files) |
| RPX SHA-1 (`game_image.cpp`) | BCrypt | a small SHA-1 of our own |
| UI font (`setup.cpp`) | Bahnschrift from `C:\Windows\Fonts` | for now the first installed of Noto Sans / DejaVu Sans / Liberation Sans; bundling an OFL font is still a decision |
| Data folder (`app_paths.cpp`) | `%APPDATA%\Cha-Cha` | `$XDG_DATA_HOME/Cha-Cha` (`~/.local/share/Cha-Cha`); `portable.txt` next to the AppImage still works |
| CRT | `_dupenv_s` (36), `_wdupenv_s` (2), `sscanf_s` (5) | `runtime/core/win_compat.h`, force-included by CMake off Windows |
| Build | `add_executable(WIN32)`; links `shell32 ole32 mfplat bcrypt windowscodecs ...` | `if(WIN32)` guards; Linux links `dl pthread avcodec avutil` |
| Packaging | `scripts/package-release.ps1` | `scripts/package-appimage.sh`: linuxdeploy with SDL3 and libavcodec, `Cha-Cha.desktop`, the icon. Not bundled: the Vulkan loader and GPU drivers (the system's) |

Files with `windows.h` to sweep: `runtime/audio/ax.cpp`, `runtime/boot/{main,profiler,read_watch,setup,
launcher_image,app_paths}.cpp`, `runtime/core/guest_memory.cpp`, `runtime/fs/{game_image,host_tree,zarchive}.cpp`,
`runtime/gfx/{gx2,presenter,renderer}.cpp`, `runtime/input/host_input.cpp`, `runtime/os/{coreinit_thread,h264,
input}.cpp`, `src/game/{boot_prompts,item_sets,text_trace}.cpp`.

## Where it is built

The build translates the player's own RPX, so a GitHub Actions runner cannot build releases: they are built
locally. On this PC: WSL2 (available, no distribution installed yet) with Ubuntu 22.04, so the AppImage runs on
glibc 2.35 and newer. The source stays on `D:` (`/mnt/d/...`); the build tree lives on WSL's own disk for speed.

## Steps

1. WSL2 Ubuntu 22.04: clang 18, CMake, Ninja, Vulkan headers + glslang, SDL3 (from source), FFmpeg dev packages,
   linuxdeploy.
2. Port the table behind small platform files (`*_win.cpp` / `*_posix.cpp`), Windows keeping its code paths.
3. Build and run headless in WSL until the game boots to the title (`--headless`, `MH3U_EXIT_AFTER_SECONDS`).
4. A windowed look through WSLg, then a real Linux machine or a Steam Deck.
5. `scripts/package-appimage.sh`; release 0.1.0 with the zip and the AppImage.
