<p align="center">
  <img src="docs/images/cha-cha.png" width="128" alt="Cha-Cha">
</p>

<h1 align="center">Cha-Cha</h1>

<p align="center">
  <b>Monster Hunter 3 Ultimate, running natively on PC.</b><br>
  A static recompilation of the Wii U game for Windows and Linux.
</p>

<p align="center">
  <a href="../../releases/latest"><img src="https://img.shields.io/github/v/release/BoyClinton/Cha-Cha?label=release&color=ecb254" alt="Latest release"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-3b332a" alt="Windows and Linux">
  <img src="https://img.shields.io/badge/graphics-Vulkan-3b332a" alt="Vulkan">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-3b332a" alt="GPL-3.0"></a>
</p>

<p align="center">
  <a href="#getting-started">Getting started</a> ·
  <a href="#whats-new-compared-to-the-original">What's new</a> ·
  <a href="#dlc-event-and-challenge-quests">DLC quests</a> ·
  <a href="#texture-packs-and-texture-dumping">Texture packs</a> ·
  <a href="#cheats">Cheats</a> ·
  <a href="#whats-next">What's next</a> ·
  <a href="#building-from-source">Building</a>
</p>

---

Cha-Cha translates the game's PowerPC code into C++ ahead of time and compiles it for your PC. It runs on our own
reimplementation of the Wii U system libraries it uses, with graphics on Vulkan and input and audio on SDL3. It is
not an emulator, and there is no emulator code in it.

> [!IMPORTANT]
> **No game files are included.** You need your own copy of **Monster Hunter 3 Ultimate (US) with update v32**.
> Cha-Cha is a fan project, not affiliated with or endorsed by Capcom or Nintendo.

## What's new compared to the original

These are part of the game and always on.

<table>
  <tr>
    <td width="33%" valign="top">
      <h3>Item Sets</h3>
      Loadouts as in MH Generations Ultimate, inside the game's own item box menu (the house box and the small
      boxes such as the Port Tanzia dock).
      <ul>
        <li><b>Register Item Set</b> saves your pouch, <b>Load Item Set</b> refills it.</li>
        <li>5 sets each for Blademaster and Gunner.</li>
        <li>Loading takes only what your pouch and box really hold, and tells you what was missing.</li>
        <li>The game's pouch window previews the result while you choose; left / right turn its pages.</li>
      </ul>
    </td>
    <td width="33%" valign="top">
      <h3>Monster HP scaling</h3>
      Port Tanzia quests are balanced for four hunters. Hunting there with fewer, monsters now have:
      <table>
        <tr><th>Hunters</th><th>HP</th></tr>
        <tr><td>1</td><td>65%</td></tr>
        <tr><td>2</td><td>80%</td></tr>
        <tr><td>3</td><td>90%</td></tr>
        <tr><td>4</td><td>100%</td></tr>
      </table>
      At every rank: Low, High and G. Moga Village quests are unchanged.
    </td>
    <td width="33%" valign="top">
      <h3>Hold to gather</h3>
      Hold the gather button on a carcass, a gathering spot or a mining point and you keep going until the spot is
      used up. No more tapping through every carve.
      <h3>DLC quests again</h3>
      The event and challenge quests, once free downloads from a server that is long gone, install from files.
      <a href="#dlc-event-and-challenge-quests">See below.</a>
    </td>
  </tr>
</table>

### Made for PC

- **Your resolution and frame rate:** internal resolution from 0.5× to 4× (up to 5120×2880), a frame rate limit up
  to unlimited, V-sync, windowed / borderless / fullscreen, smooth or sharp scaling to the window.
- **Tri look:** the softer, muted, hazier picture of Monster Hunter Tri on the Wii, applied to the world only (menus
  and the HUD stay as they are).
- **Effects the original never had:** global illumination, ambient occlusion, an enhanced depth of field and a
  vignette, each optional. The game's own bloom and depth of field can be switched off.
- **Texture packs** and texture dumping for making them. Packs switch on and off one by one, and Cemu-Enhanced packs
  work as they are.
- **Cheats:** Gateway-style codes in Cemu-Enhanced's cheat file format, ticked in the MODS panel. jM5557's MH3U
  quality-of-life cheats come bundled.
- **A real launcher:** PLAY / SETTINGS / MODS / QUIT over the game's own title screen. **F1** (or the PlayStation
  touchpad / Home button) opens the settings over the game (General > Quit game closes it), **F2** the mods, **F3** cycles the GamePad screen (hidden, picture-in-picture, GamePad only),
  **F11** switches between fullscreen and a window and frees the mouse.
- **Controls your way:** keyboard and mouse, or any controller SDL supports, played as a Wii U Pro Controller.
  Every button can be rebound, and the settings show which button does what, for your bindings and your
  controller (Xbox, PlayStation or Nintendo names).
- **Menus you can point at:** in the game's menus the mouse becomes a pointer; the option under it is selected and a
  click picks it - the start and pause menus, item box, shops, Smithy, Quest Counter, Canteen, house, Farm, Ferry,
  Supply Box and their prompts.
- **A PC keyboard** for names and chat instead of the Wii U software keyboard.

## Getting started

| | Windows | Linux |
|---|---|---|
| **Download** | `Cha-Cha-<version>-windows-x64.zip` from [Releases](../../releases/latest) | `Cha-Cha-<version>-x86_64.AppImage` from [Releases](../../releases/latest) |
| **Needs** | Windows 10 / 11 x64 | x86-64 with glibc 2.35 or newer (Ubuntu 22.04, Fedora 36, Steam Deck…) |
| **Graphics** | a Vulkan 1.2 driver | a Vulkan 1.2 driver (Mesa or your GPU vendor's) |
| **Run** | extract the zip anywhere you can write to, run `Cha-Cha.exe` | `chmod +x Cha-Cha-*.AppImage`, then run it |

1. **Start Cha-Cha.** On the first start a setup screen asks for your game.
2. **Point it at your dump:** a `.wua` archive, or the game and update folders (`code`, `content`, `meta`). Cha-Cha
   checks that it is the US v32 executable and reads the files where they are; nothing is copied.
3. **Wait a few seconds** while the game's shaders are translated for your PC. This happens once.

Your **data folder** keeps settings, saves, caches, quests and texture packs:

| | Data folder |
|---|---|
| Windows | `%APPDATA%\Cha-Cha` |
| Linux | `~/.local/share/Cha-Cha` (or `$XDG_DATA_HOME/Cha-Cha`) |
| Portable | `data/` next to the program, when a file named `portable.txt` sits beside it (the Windows zip ships with one) |

Saves live in `<data folder>/save`. To skip the setup screen, start it with `--install <path to your dump>`.

### Default controls

| Wii U Pro Controller | Keyboard and mouse | Controller |
|---|---|---|
| Left stick / right stick | WASD / O K L ; (or the mouse: click in the window, Esc releases) | sticks |
| A / B / X / Y | F, Return or right click / Space or Backspace / left click / E | the face buttons by position, as on the Wii U |
| X + A together | mouse 4 | |
| L / R / ZL / ZR | Q or Left Ctrl / Left Shift or mouse 5 / Z / C | LB / RB / LT / RT |
| + / − | Tab / M | Back / Start |
| D-pad | arrow keys, mouse wheel | d-pad |

Settings → Input switches the controller to the Xbox layout (A / Cross confirms) and rebinds anything. Bindings are
saved in `<data folder>/input.ini` ([docs/input.md](docs/input.md)).

## DLC: event and challenge quests

Capcom's download service for MH3U is gone. Cha-Cha installs the quests from files instead.

1. Open **MODS → Quests → Quest folder**. That is `<data folder>/quests`.
2. Put the quests in, each pack in a folder of its own:
   - **Official DLC:** the save files `quest1`, `quest2`, `quest3` and `quest4` from a Wii U save that has the
     downloaded quests, for example in `quests/DLC MH3U/`. Official packs install all their quests by themselves.
   - **Custom quests:** `q_<number>.quest` files, with an optional `pack.json`, `cover.png`, and a picture and
     description per quest. Custom packs let you pick quests one by one.
3. The quests appear at the **Port Tanzia Gathering Hall** with the event and challenge quests.

Pack format and options: [docs/mods.md](docs/mods.md). Quest files are game data, so Cha-Cha ships none.

## Texture packs and texture dumping

**Using a pack:** put each pack in its own folder in `<data folder>/texture_replacements` and turn on
Settings → Video → **Texture packs**. **MODS → Textures** switches each pack on and off while you play. Where two
packs replace the same texture, the one listed first wins. Packs made for Cemu-Enhanced work as they are: the same
file names, `textures.ini` (any file names, plus `[filtering]` nearest / linear per texture) and the same texture
hash. Its `load/textures/<title id>/<pack>` folders can be copied in whole. DDS files straight in the folder are
always used.

**Making a pack:**

1. Turn on Settings → Video → **Dump textures**. Every texture the game draws from then on is saved to
   `<data folder>/texture_dump` as a DDS file named after its content hash, as Cemu-Enhanced names them:
   `<hash>_<width>x<height>_fmt<format>_mip00.dds`. A note in the corner shows the folder and how many have been
   saved. Dumping lasts until you quit.
2. Edit the ones you want. Keep the name, include mipmaps, and put them in a pack folder. Or list them under any
   name in the pack's `textures.ini`.

Packs with Cha-Cha's older `tex_<width>x<height>_<CRC32>.dds` names still load. Packs made for the 3DS version
don't match the Wii U textures and won't load. Details: [docs/texture-packs.md](docs/texture-packs.md).

## Cheats

**MODS → Cheats** lists the cheats in `<data folder>/cheats/0005000010118300.txt`, the same file format
Cemu-Enhanced and Citra use (`[name]`, `{notes}`, `*cemu_enabled`, Gateway code lines). Tick one and it runs from the
next frame. Untick it and the game's original code comes back, so the *DISABLE* cheats aren't needed. The switch at
the top turns all cheats off at once.

On the first start the file is filled with [jM5557's MH3U cheats](https://github.com/jM5557/MH3U-Cemu-Cheats) for
the US game, with their quality-of-life cheats ticked: full equipment box in Port Tanzia, hold A to carve / gather,
smooth vertical camera, skip the quest end timer, send all rewards to the box, longer knockback, sharpening cancel,
interruptible flex, no reward timer. The experimental ones and *Many Zenny* are listed unticked.

Most of these cheats rewrite the game's PowerPC code. Cha-Cha runs every function a cheat has changed through a small
interpreter of its own, and everything else stays native. Addresses are for the US v32 game. Details and limits:
[docs/cheats.md](docs/cheats.md).

## What's next

- **Local network play:** hunt together with players on the same network.
- **Online play:** Cha-Cha's own lobby server in place of Nintendo's retired service
  ([docs/online.md](docs/online.md)), with HP scaling then using the real number of hunters.
- **Quest maker:** build your own quests in the game and share them with a code: copy it, send it, and anyone who
  pastes it gets the same quest.

## Building from source

The build translates **your own dump's** executable into C++, so the executable has to be there when you build. That
is one file, the US v32 `MH3G_Cafe_US.rpx` of a few MB. The rest of the game is only needed when you run Cha-Cha.

### Linux and SteamOS

On SteamOS, whose system is read-only, build in a distrobox (preinstalled since SteamOS 3.5); on any other
distribution (Arch, Ubuntu / Debian, Fedora) skip this step:

```bash
distrobox create -n chacha -i ubuntu:24.04
distrobox enter chacha          # your home folder is shared with it
```

Then, in a checkout:

```bash
scripts/install-deps-linux.sh                       # clang, CMake, Ninja, Vulkan, shaderc, FFmpeg, SDL3's dependencies
scripts/build-linux.sh --wua ~/path/to/mh3u.wua     # or: --rpx ~/path/to/MH3G_Cafe_US.rpx
scripts/package-appimage.sh                         # build/release/Cha-Cha-<version>-x86_64.AppImage
```

- `--wua` takes the executable out of your `.wua` (it builds [ZArchive](https://github.com/Exzap/ZArchive) on first
  use). `--rpx` takes the file itself: the update's `code/MH3G_Cafe_US.rpx` (title `0005000E10118300`, for example
  `mlc01/usr/title/0005000e/10118300/code/` in a Cemu install). Either way it is checked against the US v32
  executable and copied to `extracted/0005000e10118300_v32/code/MH3G_Cafe_US.elf`, so later builds need neither
  option. The EU and JP releases won't work: the code, and the cheat addresses, are the US v32 executable's.
- The build dir defaults to `~/cha-cha-build`. The generated game code takes a while to compile and about 1-2 GB of
  memory per compiler: `JOBS=4` limits the parallel compilers (the default follows the free memory). `--tests` also
  builds and runs the tests.
- shaderc comes from the distribution; the LunarG Vulkan SDK in `/opt/vulkan/<version>/x86_64` (or `$VULKAN_SDK`) is
  used instead when present.
- Run `~/cha-cha-build/runtime/boot/Cha-Cha`, or the AppImage: `chmod +x` it, and on a Steam Deck add it to Steam as a
  non-Steam game for Gaming Mode. On its first start it asks for your game (the `.wua`, or the game folders).

More: [docs/linux.md](docs/linux.md).

### Windows

1. Put your dump at `game/mh3u.wua` ([game/README.md](game/README.md)), then run
   `powershell -File scripts/extract.ps1` and `powershell -File scripts/fingerprint.ps1`.
2. Visual Studio 2026 with "Desktop development with C++" and the Vulkan SDK ([tools/README.md](tools/README.md)):

   ```
   cmake -S . -B build/cmake
   cmake --build build/cmake --config Release
   ctest --test-dir build/cmake -C Release --output-on-failure
   ```

   A clang-cl build (`cmake -S . -B build/clang -T ClangCL`) compiles the generated code faster.
   `scripts/package-release.ps1` makes the release zip.

Inside the checkout Cha-Cha finds `extracted/` by itself and uses the checkout as its data folder (all of it
gitignored). The release packages never contain game files, but the program itself contains the recompiled game
code, translated from your executable at build time.

<details>
<summary><b>Repository layout</b></summary>

| Path | What lives there | In git? |
|---|---|---|
| `game/`, `extracted/` | Your dump, and its unpacked contents | no |
| `config/` | Target hashes, symbol map, function list, known structures | yes |
| `recomp/` | The recompiler; `recomp/out/` is the generated C++ | source only |
| `runtime/` | The PC layer replacing the Wii U system: `os`, `gfx` (GX2 on Vulkan), `audio`, `input`, `fs`, `boot`, `cheats` | yes |
| `src/` | Hand-written game code: item sets, hold to gather, HP scaling… | yes |
| `assets/cheats/` | The bundled MH3U US cheat file (jM5557's, written to the data folder on the first start) | yes |
| `tools/`, `scripts/`, `ghidra/` | Shader translator, helper scripts, reverse-engineering scripts | yes |
| `docs/` | Roadmap, research log, formats, mods, input, Linux, online notes | yes |
| `build/` | Build output | no |

How the port works and what was learned: [docs/runtime.md](docs/runtime.md), [docs/engine.md](docs/engine.md),
[docs/research-log.md](docs/research-log.md).

</details>

## Credits

- **Dusklight**: the Twilight Princess PC port whose front end (launcher, title
  screen, settings window) Cha-Cha's front end follows.
- **Yakumo**: the asset-free release model Cha-Cha follows: a release build plus your own dump, nothing from the
  game shipped.
- **jM5557**: the MH3U cheats bundled with Cha-Cha, and Cemu-Enhanced, whose cheat file format and texture pack
  format (names, `textures.ini`, content hash) Cha-Cha reads.
- **Ghidra**, for reverse engineering.
- **Libraries:** SDL3, Dear ImGui, zstd, the Vulkan SDK (shaderc, glslang, SPIRV-Tools), FFmpeg's libavcodec on
  Linux ([docs/release/THIRD_PARTY_NOTICES.md](docs/release/THIRD_PARTY_NOTICES.md)).

Monster Hunter is a trademark of Capcom. Wii U is a trademark of Nintendo.

## License

Cha-Cha is free software under the **GNU General Public License v3.0** ([LICENSE](LICENSE)): you may use, study,
change and share it, and if you share a changed version you share its source under the same licence. Third-party
libraries keep their own licences. The licence covers Cha-Cha's own code only, not the game: Monster Hunter 3
Ultimate and everything translated from it remain Capcom's.
