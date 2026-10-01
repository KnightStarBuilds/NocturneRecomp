#!/usr/bin/env bash
set -euo pipefail
config=Debug
clean=0
run=0
help=0
for argument in "$@"; do
    case "$argument" in
        --config=Debug|--config=Release) config="${argument#*=}" ;;
        --clean) clean=1 ;;
        --run) run=1 ;;
        --help|-h) help=1 ;;
        *) echo "Unknown build argument: $argument" >&2; exit 2 ;;
    esac
done
if (( help )); then echo 'Usage: ./build.sh [--config=Debug|Release] [--clean] [--run]'; exit 0; fi
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
case "$(uname -s)" in
    Darwin) platform=macos ;;
    Linux) platform=linux ;;
    MINGW*|MSYS*) platform=windows ;;
    *) echo 'Unsupported build host' >&2; exit 2 ;;
esac
architecture="$(uname -m)"
[[ "$architecture" != aarch64 ]] || architecture=arm64
build="$root/build/$platform-$architecture"
command -v cmake >/dev/null
command -v ninja >/dev/null
command -v python3 >/dev/null
compiler_args=()
if [[ "$platform" == macos ]]; then
    # Shell profiles may export Homebrew LLVM flags; the native lane owns its SDK.
    unset CC CXX CPPFLAGS CFLAGS CXXFLAGS LDFLAGS
    compiler_args=("-DCMAKE_C_COMPILER=$(xcrun --find clang)"
                   "-DCMAKE_CXX_COMPILER=$(xcrun --find clang++)"
                   "-DCMAKE_OSX_ARCHITECTURES=$architecture" "-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0"
                   "-DCMAKE_OSX_SYSROOT=$(xcrun --show-sdk-path)"
                   "-DCMAKE_EXE_LINKER_FLAGS=" "-DCMAKE_SHARED_LINKER_FLAGS="
                   "-DCMAKE_MODULE_LINKER_FLAGS=")
elif [[ "$platform" == linux && "$architecture" == x86_64 ]]; then
    compiler_args=("-DCMAKE_TOOLCHAIN_FILE=$root/src/cmake/toolchains/linux-x86_64.cmake")
elif [[ "$platform" == windows ]]; then
    compiler_args=("-DCMAKE_TOOLCHAIN_FILE=$root/src/cmake/toolchains/windows-msys2.cmake")
fi
# Arguments and tool availability are checked before deleting platform outputs.
if (( clean )); then rm -rf -- "$build"; fi
prepare_args=(--build-dir "$build")
[[ "${NOCTURNE_OFFLINE:-0}" != 1 ]] || prepare_args+=(--offline)
python3 "$root/deps/prepare.py" "${prepare_args[@]}"
cmake -S "$root" -B "$build" -G 'Ninja Multi-Config' "${compiler_args[@]}"
cmake --build "$build" --config "$config" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-8}"
if (( run )); then exec "$root/run.sh" "--config=$config"; fi
