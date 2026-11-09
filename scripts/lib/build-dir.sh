# shellcheck shell=bash
# shellcheck disable=SC2154  # src and die() come from the script that sources this
# Shared by the build scripts (sourced). A CMake build folder belongs to the checkout it was first made from, so with
# more than one checkout (Cha-Cha, Cha-Cha-Enhanced, the Android one) a default folder may already be another's.
#   mh3u_build_dir <default folder> <folder given on the command line, or empty> <subfolder>:<source folder> ...
# sets `build`: the given folder (stopping if it belongs to another checkout); else the default when it is unused or
# this checkout's; else <default>-<this checkout's folder name>, said so.
mh3u_cache_source() { sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$1/CMakeCache.txt" 2>/dev/null | head -1; }

# prints the checkout a build folder (or one of its subfolders) was made from, if that is not this one
mh3u_foreign_build() {
    local root="$1" pair sub want have
    shift
    for pair in "$@"; do
        sub="${pair%%:*}"
        want="${pair#*:}"
        have="$(mh3u_cache_source "$root/$sub")"
        if [ -n "$have" ] && [ "$(realpath -m "$have")" != "$(realpath -m "$want")" ]; then
            echo "${have%"${want#"$src"}"}"  # .../Cha-Cha/recomp -> .../Cha-Cha
            return 0
        fi
    done
    return 1
}

mh3u_build_dir() {
    local default="$1" given="$2" other
    shift 2
    if [ -n "$given" ]; then
        if other="$(mh3u_foreign_build "$given" "$@")"; then
            die "$given was made from another checkout ($other).
Pass another folder (scripts/$(basename "$0") <folder>), or delete that one: rm -rf \"$given\""
        fi
        build="$given"
        return
    fi
    build="$default"
    if other="$(mh3u_foreign_build "$build" "$@")"; then
        build="$default-$(basename "$src")"
        if mh3u_foreign_build "$build" "$@" >/dev/null; then
            die "$default and $build both belong to other checkouts. Pass a folder: scripts/$(basename "$0") <folder>"
        fi
        echo "note: $default is $other's build; this checkout builds in $build"
    fi
}
