# Texture packs

Cha-Cha replaces the game's textures with DDS files while it runs. Packs work the way they do in Cemu-Enhanced: the
same file names, the same `textures.ini` and the same hash. A pack moves between the two unchanged.

## Where packs go

The data folder is `%APPDATA%\Cha-Cha` for a release, `data\` next to the exe in portable mode, and the repo root
when run from a source checkout. `MH3U_TEXTURE_DIR` points runs elsewhere.

```
<data folder>/texture_replacements/
  My Pack/...                        each folder is a pack
  0005000010118300/Other Pack/...    Cemu-Enhanced's load/textures/<title id>/<pack> folders, copied as they are
  *.dds                              loose files: always used, any pack overrides them
```

**MODS → TEXTURES** lists the packs, each with a switch. The choice is saved in `mods.ini` as `texpack.<name>=off`,
and a pack not listed is on. Switching one applies from the next frame. **SETTINGS → Video → Texture packs** turns
replacement on and off as a whole.

Where two packs that are on replace the same texture, the pack whose name sorts first wins (the order in the
list). The log names the overlap. Subfolders inside a pack are fine.

## Making a pack

1. Turn on **SETTINGS → Video → Dump textures** and play to where the texture appears. Every texture the game draws
   is saved to `<data folder>/texture_dump` as a DDS file named after its content hash:

   ```
   22fb5c1e481b3338_1280x720_fmt0031_mip00.dds
   ```

   That is the 16-digit hash, the size, the GX2 surface format and the mip level, as Cemu-Enhanced names its dumps.
   It starts at once and lasts for this session. Textures already loaded are dumped the next time they are drawn,
   and toasts show the folder and a running count. For runs, `MH3U_DUMP_TEXTURES=1` does the same, and
   `MH3U_DUMP_TEXTURES=<folder>` writes somewhere else. Dumps are level 0 in the game's own format (BC1–BC5 or RGBA8)
   and open in Paint.NET, GIMP and texconv. Videos and other textures the CPU rewrites every frame, cube maps and
   render targets are not dumped.
2. Edit the ones you want. Keep the name and put the files in a pack folder. Include mipmaps, or the texture
   shimmers in the distance: only the levels that are present are used.

## textures.ini: any file names

A `textures.ini` at the top of a pack lets you name and arrange the files however you like (PPSSPP's format):

```ini
[hashes]
# Menus
22fb5c1e481b3338 = menu/background1.dds
22fb5c1e481b3338_1280x720_fmt0031_mip01 = menu/background1_mip1.dds

[filtering]
22fb5c1e481b3338 = linear
6a0d3c21f9e4b7a5 = nearest
```

- **Keys** are the 16-digit hash (mip 0, whose DDS can carry the whole mip chain), or a whole dump name to give a
  single mip level its own file.
- **Paths** are relative to the pack and must stay inside it. `/` and `\` both work.
- `#` starts a comment. Within a pack, a `textures.ini` entry beats a file named by its hash.
- **[filtering]** forces how the game samples a texture: `nearest` (sharp pixels, for fonts and pixel art), `linear`,
  or `auto` (the game's own). It applies whether or not the pack also replaces the texture.
- Lines that don't make sense (a key that is not a hash, a missing file, a path outside the pack, an unknown
  filter) are skipped and named in the log.

## The hash

The hash covers every byte of the texture's level 0 as it sits in guest memory: GX2-tiled, over the surface's
image size. It is mixed with the pixel count, the GX2 surface format and that size, so textures with the same bytes
but a different shape never match. It is bit-identical to Cemu-Enhanced's (`gfx_texture_packs` checks known
values). Packs made before Cemu-Enhanced mixed the surface in, which are named by the plain content hash, still
load, but only for a texture whose size and format match what their file names record.

The hash names cover the formats Cemu-Enhanced replaces: BC1–BC5 (block-compressed) and RGBA8. Cha-Cha's older names
also still load. These are `tex_<width>x<height>_<CRC32>[anything].dds`, the CRC32 of the same level-0 bytes.

## Notes

- DDS formats: BC1–BC5 and BC7, from a DX10 header or the DXT1/3/5 and ATI1/2 FourCCs, and RGBA8 / BGRA8. sRGB vs
  UNORM follows the original texture, whatever the file says.
- A replacement may be larger than the original (keeping the aspect ratio is wise), and its format may differ.
- A replacement is loaded the first time its texture is uploaded, and again when the game reloads it. Textures the
  game rewrites every frame (CPU animations) and render targets are not replaced.
- Packs made for Monster Hunter 3 Ultimate on 3DS don't load: their names are hashes of 3DS texture data, which never
  match the Wii U textures.
- Packs are game art: keep them out of git (`texture_replacements/` and `texture_dump/` are ignored).

Code: `runtime/gfx/texture_pack.*` (hash, packs, `textures.ini`; `texture_pack_test`), `replace_texture()` and the
sampler override in `runtime/gfx/renderer.cpp`, the TEXTURES tab in `runtime/boot/setup.cpp`.
