# Cheats

Cha-Cha reads Gateway-style cheat codes. It uses the per-game text files that Citra / Azahar use for 3DS cheats, in
the form Cemu-Enhanced reads them. A cheat file made for Cemu-Enhanced works here unchanged.

## Where

**MODS → CHEATS** (F2 in the game) lists every cheat in the file. Ticking one saves the file at once, and the
running game picks the change up on its next frame. The **Cheats** switch at the top turns them all off without
changing which ones are ticked. It is saved in `mods.ini` as `cheats=on|off`.

The file is `<data folder>/cheats/0005000010118300.txt` (the US title ID). The CHEATS tab opens its folder. An
existing upper-case name, `0005000010118300.TXT` as Citra writes it, is used when there is no lower-case one.

**Bundled cheats.** On the first start, when there is no cheat file yet, Cha-Cha writes jM5557's MH3U US cheats
there ([MH3U-Cemu-Cheats](https://github.com/jM5557/MH3U-Cemu-Cheats), `assets/cheats/0005000010118300.txt` in this
repository). The author's defaults are kept: the QOL cheats start ticked. To start over with the bundled file,
delete yours.

## File format

```
[Equipment box in Port Tanzia tavern]
{Optional notes,
 may span lines}
*cemu_enabled
027996D0 38A00000
```

- `[name]` starts a cheat. `{notes}` is optional and may span lines.
- `*cemu_enabled` (or `*citra_enabled`) marks it as on.
- Each code line is two 8-digit hex numbers.

## Code types

The Wii U is big-endian: `0XXXXXXX 38A00000` stores the bytes `38 A0 00 00`, which is how a PowerPC instruction is
encoded. Addresses carry 28 bits, and the offset register reaches higher memory (`D3000000 10000000`).

| Code | Meaning |
|---|---|
| `0XXXXXXX YYYYYYYY` | 32-bit write `[X+offset] = Y` |
| `1XXXXXXX 0000YYYY` | 16-bit write |
| `2XXXXXXX 000000YY` | 8-bit write |
| `3`–`6XXXXXXX YYYYYYYY` | if `[X+offset]` `<`, `>`, `==`, `!=` Y (32-bit) |
| `7`–`AXXXXXXX ZZZZYYYY` | the same, 16-bit, ignoring the bits set in mask Z |
| `BXXXXXXX 00000000` | `offset = [X+offset]` |
| `C0000000 YYYYYYYY` | repeat the block up to the next `D1` / `D2` Y times |
| `D0` / `D1` / `D2` | end if / end loop / end everything (clears offset and data) |
| `D3` / `DC 000000 YYYYYYYY` | `offset = Y` / `offset += Y` |
| `D4` / `D5 000000 YYYYYYYY` | `data += Y` / `data = Y` |
| `D6` / `D7` / `D8 000000 YYYYYYYY` | write data (32 / 16 / 8-bit) to `[Y+offset]`, then advance the offset |
| `D9` / `DA` / `DB 000000 YYYYYYYY` | load data (32 / 16 / 8-bit) from `[Y+offset]` |
| `EXXXXXXX YYYYYYYY` | copy the Y bytes on the following lines to `X+offset` |

`DD` (3DS button conditions) is rejected. A code that does not parse is not run. The CHEATS tab names the line and
the reason, as it does for a code that touches unmapped memory. A cheat with an unmapped address keeps being retried
every frame: it may point at a heap block the game has not allocated yet.

## Cheats that patch code

Almost all MH3U cheats change PowerPC instructions, and most add code in caves at `0x01800000`–`0x01FFFFFF`. In
Cha-Cha the game's code is translated to C++ when Cha-Cha is built, so changing instruction bytes in memory would do
nothing by itself. So:

1. Enabled cheats run once per frame on the game's graphics thread (`GX2SwapScanBuffers`). A write that changes
   nothing is skipped.
2. After each run, the engine compares the code region with the executable as it was mapped at boot. It flags every
   function whose instructions now differ. The owning function comes from the generated `kFunctionRanges`, which
   includes multi-range bodies.
3. Every recompiled function checks that flag on entry: one relaxed load while no cheat has patched anything. A
   flagged function runs from guest memory through the interpreter (`recomp/support/interpreter.cpp`): the patched
   instructions, the jumps into a cave, and back out again. Its calls go back to the recompiled functions. Only
   flagged functions are slower.
4. When a cheat is turned off, or the master switch is, the original instructions it wrote are put back. The
   functions become recompiled code again. A *DISABLE* cheat is therefore not needed in Cha-Cha, though running one
   does no harm.

The interpreter mirrors the recompiler's translation op for op. `interp_selftest` runs every emitter self-test
function both ways from many states and requires identical results. `cheats_test` reads the bundled file: all 37
cheats compile. Of the 1,197 words they write into code or caves, 1,186 are instructions the interpreter runs. The
other 11 are data the cave code reads (timers, counts).

Limits:

- A patch takes effect at the function's **next call**. A function that never returns (a thread's main loop) keeps
  running its old code.
- A function with a hand-written replacement in `src/game` (`config/overrides.txt`) is interpreted only when the
  replacement calls the original, which all of the wrappers do. Patches to `cameraPitchInput?`, `cameraTurnInput?`
  and `sResult::rewardStep?` then run inside Cha-Cha's frame-time camera scaling and menu pointer code.
- Addresses are for the US v32 executable, the only one Cha-Cha runs. EU / JP cheats need their addresses found
  again.
- Heap addresses can differ from Cemu's. Cha-Cha has its own heap implementation, so a cheat that writes to a fixed
  heap address (*Many Zenny*: `0x2E62CBD8`) may hit something else. Cheats that follow a pointer from static data, as
  the *Frame cap 44 FPS* cheat does, are unaffected.
- Some bundled cheats duplicate a built-in change: *Hold A to Carve/Gather* (Cha-Cha has hold to gather) and
  *Frame cap 44 FPS* (Cha-Cha paces frames itself; SETTINGS → Video → Frame rate limit).

Code: `runtime/cheats/cheats.*` (file, codes, engine; `cheats_test`); `recomp/support/interpreter.cpp` and the
patched-function flags in `recomp/support/indirect.cpp` (`interp_selftest`); the entry check emitted by
`recomp/src/emitter.cpp` (`EmitEnv::patch_hook`); the CHEATS tab in `runtime/boot/setup.cpp`; boot wiring in
`runtime/boot/main.cpp` (`setup_cheats`).
