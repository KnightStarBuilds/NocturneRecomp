#!/usr/bin/env python3
"""Cross-build the native PS4 application using pinned OpenOrbis dependencies."""
import argparse
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build/ps4-x86_64"


def run(command, **kwargs):
    return subprocess.run([str(item) for item in command], check=True, **kwargs)


def tool(name, override=None):
    selected = override or shutil.which(name)
    if not selected or not Path(selected).is_file() or not os.access(selected, os.X_OK):
        raise RuntimeError(f"Required tool not found: {name}")
    # LLVM's multicall tools select their behavior from argv[0]. Resolving
    # llvm-ranlib -> llvm-ar or ld.lld -> lld would select the wrong command.
    return str(Path(selected).absolute())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", choices=("Debug", "Release"), default="Debug")
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args()
    if platform.system() not in ("Darwin", "Linux"):
        parser.error("Use macOS or Linux/WSL for the PS4 cross-build")
    # Validate all host inputs before preparation or removal of a previous build.
    cmake, ninja, make = tool("cmake"), tool("ninja"), tool("make")
    llvm = os.environ.get("PS4_LLVM_DIR")
    if not llvm and platform.system() == "Darwin" and shutil.which("brew"):
        candidate = run(["brew", "--prefix", "llvm"], capture_output=True, text=True).stdout.strip()
        if Path(candidate, "bin/clang").is_file():
            llvm = str(Path(candidate, "bin"))
    # Homebrew installs LLD separately from LLVM; find it on PATH when it is
    # absent from the selected compiler directory.
    compilers = {name: tool(name, str(Path(llvm, name)) if llvm and Path(llvm, name).is_file() else None)
                 for name in ("clang", "clang++", "ld.lld", "llvm-ar", "llvm-ranlib")}
    for executable in (ninja, make, compilers["ld.lld"], compilers["llvm-ar"], compilers["llvm-ranlib"]):
        run([executable, "--version"], capture_output=True, text=True)
    version = run([compilers["clang++"], "--version"], capture_output=True, text=True).stdout
    if "Apple clang" in version:
        raise RuntimeError("PS4 requires LLVM clang/LLD, not Apple Clang; install LLVM or set PS4_LLVM_DIR")
    version = run([cmake, "--version"], capture_output=True, text=True).stdout
    match = re.search(r"cmake version (\d+)\.(\d+)", version)
    if not match or tuple(map(int, match.groups())) < (3, 24):
        raise RuntimeError("CMake 3.24 or later is required")
    jobs = os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL", "8")
    if not jobs.isdecimal() or int(jobs) < 1:
        raise RuntimeError("CMAKE_BUILD_PARALLEL_LEVEL must be a positive integer")
    if BUILD.is_symlink():
        raise RuntimeError("Refusing a symlink at build/ps4-x86_64")
    external_sdk = os.environ.get("OO_PS4_TOOLCHAIN")
    if external_sdk:
        sdk = Path(external_sdk).resolve()
        if not (sdk / "lib/crt1.o").is_file():
            raise RuntimeError("OO_PS4_TOOLCHAIN must contain lib/crt1.o")
        if args.clean and sdk.is_relative_to(BUILD):
            raise RuntimeError("--clean cannot remove an explicitly selected SDK inside the PS4 build tree; unset OO_PS4_TOOLCHAIN")
    prepare = [sys.executable, ROOT / "deps/prepare.py", "--build-dir", BUILD, "--target=ps4"]
    if os.environ.get("NOCTURNE_OFFLINE") == "1":
        prepare.append("--offline")
    # Cache-only preparation verifies/downloads everything before --clean can
    # remove output, and leaves the build and original game data untouched.
    run(prepare + ["--verify-cache-only"])
    sdk = Path(external_sdk).resolve() if external_sdk else ROOT / "deps/cache/openorbis/PS4Toolchain"
    host = "macos" if platform.system() == "Darwin" else "linux"
    converter = sdk / "bin" / host / ("create-fself-macos" if host == "macos" else "create-fself")
    if not converter.is_file():
        raise RuntimeError(f"Missing PS4 executable converter: {converter}")
    env = os.environ.copy()
    for key in ("CC", "CXX", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "SDKROOT", "MACOSX_DEPLOYMENT_TARGET"):
        env.pop(key, None)
    env["OO_PS4_TOOLCHAIN"] = str(sdk)
    probe = subprocess.run([str(converter), "--help"], env=env, capture_output=True, text=True)
    if probe.returncode not in (0, 2) or "Usage" not in probe.stderr + probe.stdout:
        raise RuntimeError("OpenOrbis create-fself cannot execute; on Apple Silicon verify Rosetta is installed\n" + probe.stderr)
    sdk_overlay = BUILD / "sdk-include"
    prepare_sdk = [sys.executable, ROOT / "ps4/prepare_sdk.py", "--output", sdk_overlay]
    run(prepare_sdk + ["--sdk", sdk, "--check-only"])
    if args.clean and BUILD.exists():
        shutil.rmtree(BUILD)
    run(prepare)
    sdk = Path(external_sdk).resolve() if external_sdk else BUILD / "dependency-sources/openorbis/PS4Toolchain"
    run(prepare_sdk + ["--sdk", sdk])
    env["OO_PS4_TOOLCHAIN"] = str(sdk)
    run([cmake, "-S", ROOT, "-B", BUILD, "-G", "Ninja Multi-Config",
         "-DCMAKE_TOOLCHAIN_FILE=" + str(ROOT / "ps4/cmake/toolchain.cmake"),
         "-DOO_PS4_TOOLCHAIN=" + str(sdk), "-DCMAKE_C_COMPILER=" + compilers["clang"],
         "-DNOCTURNE_PS4_SDK_INCLUDE=" + str(sdk_overlay),
         "-DCMAKE_CXX_COMPILER=" + compilers["clang++"], "-DCMAKE_LINKER=" + compilers["ld.lld"],
         "-DCMAKE_AR=" + compilers["llvm-ar"], "-DCMAKE_RANLIB=" + compilers["llvm-ranlib"],
         "-DCMAKE_MAKE_PROGRAM=" + ninja, "-DCMAKE_SKIP_RPATH=ON"], env=env)
    run([cmake, "--build", BUILD, "--config", args.config, "--parallel", jobs], env=env)
    print(f"PS4 {args.config} built. Package with ./ps4/package.sh --config={args.config}")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"PS4 build: {error}", file=sys.stderr)
        sys.exit(1)
