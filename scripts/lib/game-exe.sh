# shellcheck shell=bash
# shellcheck disable=SC2154  # src, rpx, wua and die() come from the script that sources this
# Shared by scripts/build-linux.sh and scripts/build-android.sh (sourced): puts the game's executable where the build
# reads it, extracted/0005000e10118300_v32/code/MH3G_Cafe_US.elf, from --rpx FILE or --wua FILE, checked against the
# US v32 hash in config/target.txt. Needs: src, rpx, wua (may be empty) and die(); git, cmake, ninja for ZArchive.
mh3u_place_executable() {
    local target_dir="$src/extracted/0005000e10118300_v32/code"
    local target="$target_dir/MH3G_Cafe_US.elf"
    local want_sha
    want_sha="$(grep -i '_v32' "$src/config/target.txt" | grep -o 'sha1=[0-9a-f]*' | head -1 | cut -d= -f2)"  # US v32 .rpx
    if [ -n "$wua" ]; then
        [ -f "$wua" ] || die "no such file: $wua"
        local zarchive tools tmp found
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
        echo "extracting $wua (the whole archive, then only the executable is kept)"
        "$zarchive" "$wua" "$tmp/out" >/dev/null
        rpx="$(find "$tmp/out" -ipath '*0005000e10118300*' -iname 'MH3G_Cafe_US.rpx' | head -1)"
        if [ -z "$rpx" ]; then
            found="$(find "$tmp/out" -iname '*.rpx' | sed "s|$tmp/out/||")"
            rm -rf "$tmp"
            die "no US update executable (0005000e10118300.../code/MH3G_Cafe_US.rpx) in the .wua; it has: ${found:-no .rpx}
Cha-Cha needs Monster Hunter 3 Ultimate US with update v32 in the archive."
        fi
        mkdir -p "$target_dir"
        cp "$rpx" "$target_dir/.from-wua.rpx"
        rm -rf "$tmp"
        rpx="$target_dir/.from-wua.rpx"
    fi
    if [ -n "$rpx" ]; then
        [ -f "$rpx" ] || die "no such file: $rpx"
        local sha
        sha="$(sha1sum "$rpx" | cut -d' ' -f1)"
        if [ -n "$want_sha" ] && [ "$sha" != "$want_sha" ] && [ -z "${MH3U_SKIP_SHA_CHECK:-}" ]; then
            die "$rpx is not the US v32 executable (SHA-1 $sha, expected $want_sha from config/target.txt).
The EU (0005000010117200) and JP releases and other updates are not supported. (An already decompressed
.elf of the right executable has another hash: MH3U_SKIP_SHA_CHECK=1 accepts it.)"
        fi
        mkdir -p "$target_dir"
        cp "$rpx" "$target"
        rm -f "$target_dir/.from-wua.rpx"
        echo "executable: ${wua:-$rpx} -> extracted/0005000e10118300_v32/code/MH3G_Cafe_US.elf"
    fi
    [ -f "$target" ] || die "the game's executable is missing at $target
Pass --rpx <MH3G_Cafe_US.rpx> or --wua <your dump.wua> (US game with update v32). Only this one file is needed
to build; the rest of the game is chosen when Cha-Cha starts."
}
