#!/usr/bin/env bash
set -euo pipefail
config=Debug
help=0
while (( $# )); do
    case "$1" in
        --config=Debug|--config=Release) config="${1#*=}"; shift ;;
        --) shift; break ;;
        --help|-h) help=1; shift ;;
        *) echo "Unknown launcher argument: $1 (game arguments follow --)" >&2; exit 2 ;;
    esac
done
if (( help )); then echo 'Usage: ./run.sh [--config=Debug|Release] [-- arguments]'; exit 0; fi
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
case "$(uname -s)" in
    Darwin) platform=macos ;;
    Linux) platform=linux ;;
    MINGW*|MSYS*) platform=windows ;;
    *) echo 'Unsupported launch host' >&2; exit 2 ;;
esac
architecture="$(uname -m)"
[[ "$architecture" != aarch64 ]] || architecture=arm64
build="$root/build/$platform-$architecture"
if [[ "$platform" == macos ]]; then
    executable="$build/bin/$config/NocturneRecompKnightStar.app/Contents/MacOS/NocturneRecompKnightStar"
else
    executable="$build/bin/$config/nocturne_recomp_knightStar"
    [[ "$platform" != windows ]] || executable+=.exe
fi
[[ -x "$executable" ]] || { echo "Build $config first with ./build.sh --config=$config" >&2; exit 127; }
exec "$executable" --data-dir "$root/data/original" "$@"
