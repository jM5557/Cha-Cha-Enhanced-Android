# Android

Cha-Cha builds as an Android app (arm64) from the same source as the Windows and Linux versions. The cheats, texture
packs, quests and every built-in change come along.

> [!IMPORTANT]
> The Android build has been checked as far as is possible without a phone and a dump. The shared code compiles for
> arm64 with the NDK's C++ library version. The recompiler, interpreter, cheat, texture and guest-memory tests pass on
> arm64, also with 16 KB pages, under qemu. The Java compiles. It has not yet run on a device: the first runs are the
> real test. Send `log.txt` (below) when something goes wrong.

## What it needs

- **A phone or handheld:** arm64, Android 10 or newer, Vulkan 1.1 or newer. The game's heap alone is 1 GB of guest
  memory, so a device with 6 GB of RAM or more is advisable.
- **A controller.** There are no touch controls yet. Handhelds with built-in controls (Retroid, AYN, Ayaneo) and
  Bluetooth or USB controllers work through SDL. Taps on the screen work the launcher and point at the game's menus,
  but don't press game buttons.
- **Your dump:** Monster Hunter 3 Ultimate US with update v32, as for the PC versions. The executable is translated
  into the app when it is built; the rest of the game is read on the phone.

## Building (on Linux, e.g. Arch)

```bash
scripts/install-deps-linux.sh --android             # compilers, CMake, Ninja, a JDK 17, unzip...
scripts/build-android.sh --wua ~/path/to/mh3u.wua   # or --rpx MH3G_Cafe_US.rpx; --install puts it on a USB-connected phone
```

The APK lands in `build/release/Cha-Cha-<version>-arm64.apk`. The first build fetches, once:

- **The Android SDK and NDK**, into `$ANDROID_HOME` (default `~/Android/Sdk`). The script downloads the command-line
  tools, then `sdkmanager` installs platform 35, build tools 35.0.0 and the newest NDK. NDK r28 or newer is needed,
  for C++20's `std::atomic_ref`. You accept Google's SDK licences on the way.
- **shaderc** (v2026.3, with its glslang / SPIRV-Tools), from GitHub. It translates the game's shaders on the phone
  at the first start.
- **FFmpeg** (n7.1, the H.264 decoder only), from GitHub, for the game's movies.

shaderc and FFmpeg are kept in `~/.cache/cha-cha-tools/android`. Then the recompiler runs on your PC (a host build
of `recomp_emit`), the game code is compiled for arm64 (as long as the Linux build; `JOBS` limits the parallel
compilers), and the APK is put together without Gradle: `aapt2`, `javac`, `d8`, `zipalign` and `apksigner`.

**Signing.** The APK is signed with a key made on your PC the first time (`~/.cache/cha-cha-tools/android/debug.keystore`).
Keep that file: an APK signed with another key won't install over this one, so you would have to uninstall first.

Options: `APP_ID=...` changes the package name (default `org.chacha.mh3u`), `ANDROID_HOME` / `ANDROID_NDK_HOME`
pick an existing SDK / NDK, `JOBS=4` limits the compilers.

## Installing and the first start

1. Copy the APK to the phone and open it (allow installs from the file manager), or `--install` over USB with USB
   debugging on.
2. **The game.** Either:
   - put your `.wua` (or the game folders `code`, `content`, `meta`) in the data folder's `game/` folder, and Cha-Cha
     finds it by itself with no permission needed; or
   - leave it in `Download`, `Documents`, `Games`, `ROMs` or `Emulation` on the internal storage and grant **All files
     access** when Cha-Cha asks. CHOOSE GAME then opens the system's file picker. Pick the file from the internal
     storage or the SD card in the picker's menu, not from Recent or Downloads, which hide the file's real path.
3. The first start translates the game's shaders (a minute or two on a phone).

## The data folder

Settings, saves, caches, `log.txt`, `cheats/`, `texture_replacements/` and `quests/` live in the data folder:

- **`/storage/emulated/0/Cha-Cha`**, if that folder exists and Cha-Cha has All files access. It stays when the app is
  uninstalled, and any file manager reaches it. Create it, grant the permission, and Cha-Cha uses it from the next
  start. Move your files over from the other folder yourself.
- Otherwise **`/storage/emulated/0/Android/data/org.chacha.mh3u/files`**. No permission is needed, and a PC sees it
  over USB, but Android hides it from most file managers and **deletes it with the app**. Back up `save/` before
  uninstalling.

The MODS buttons that open folders say where the folder is instead (Android has no file manager to hand them to).

## Controls

- The controller plays the game as on the PC: a Wii U Pro Controller layout, rebindable in SETTINGS → Input.
- **Back** (the system back button or gesture), or the controller's Home / guide button, opens the settings over the
  game. **SETTINGS → General → Mods** opens MODS (F2 on the PC), with Quests, Cheats and Textures.
- When Cha-Cha goes to the background the game pauses, and it carries on when you come back.

## What is different on a phone

- **BC textures.** Most phone GPUs (Adreno, Mali) can't sample BC1-BC5 textures, which the game uses throughout. The
  renderer decodes them on the CPU when they are loaded (`runtime/gfx/bc_decode.*`), and texture packs' BC files too.
  BC7 pack files can't be decoded: use BC1-BC3 or RGBA8 for packs meant for phones.
- **Older Vulkan drivers.** Vulkan 1.1 drivers get SPIR-V 1.3 shaders. Drivers without
  `VK_EXT_shader_demote_to_helper_invocation` get `discard` in place of `demote` (`translate.h`, `Target`). The cache
  remembers which, and is redone for another kind of device. Dynamic rendering and synchronization2 are emulated where
  missing, as for old PC GPUs.
- **Memory ordering.** arm64, unlike x86, reorders memory accesses between threads, so the game's own `sync` / `isync`
  / `eieio` are real fences there (`ppc::memory_barrier`).
- **Guest memory** uses the host's page size: 4 KB, or 16 KB on newer phones. On arm64 the write-tracking fault handler
  tells writes from reads by the fault's ESR (or, under emulators, the instruction). Libraries are aligned for 16 KB
  pages.

## When something goes wrong

- `log.txt` in the data folder has everything the PC version prints to its console: Vulkan device, shader
  translation, `cheats:` lines, crashes with the faulting address.
- `adb logcat -s SDL SDL/APP DEBUG` shows SDL's messages and native crash reports.
- To symbolise a native crash, use `~/cha-cha-android-build/android/runtime/boot/libmain.so` (the APK's copy is
  stripped).

## How it is built (for working on it)

| Piece | Where |
|---|---|
| Native library | `runtime/boot/CMakeLists.txt`: `libmain.so` on Android (`add_library(mh3u SHARED)`), `main()` becomes `SDL_main` (`SDL_main.h` in `main.cpp`) |
| Host tools | `recomp/CMakeLists.txt` builds on its own (`cmake -S recomp`); a cross build imports `recomp_emit` and `emit_selftest_gen` from `MH3U_HOST_TOOLS` |
| shaderc | `tools/shaderc-latte/CMakeLists.txt`: `MH3U_SHADERC_SOURCE` (`add_subdirectory`) |
| FFmpeg | `runtime/os/CMakeLists.txt`: `MH3U_FFMPEG_DIR` (static `libavcodec.a` / `libavutil.a`) |
| Java | `android/java/org/chacha/mh3u/ChaChaActivity.java` (SDL3's `SDLActivity` plus the storage permission), compiled with SDL's Java from the fetched SDL source |
| Manifest, resources | `android/AndroidManifest.xml`, `android/res/`; the icon comes from `icon/icon.ico` |
| Platform code | `runtime/boot/android_platform.*` (data folder, permission, JNI), `runtime/boot/android_paths.*` (picker URIs to paths, dump search; `android_paths_test`) |
| Lifecycle | `runtime/gfx/presenter.cpp`: the surface is dropped on `SDL_EVENT_WILL_ENTER_BACKGROUND` (event watch) and made again on the next present; `GX2SwapScanBuffers` waits while paused |

Tests that run anywhere and matter for Android: `core_guest_memory`, `gfx_bc_decode`, `boot_android_paths`,
`interp_selftest`, `cheats`. For arm64 they can be cross-compiled and run under `qemu-aarch64 -L
/usr/aarch64-linux-gnu`, with `-p 16384` for 16 KB pages.
