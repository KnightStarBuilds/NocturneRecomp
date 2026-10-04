#!/usr/bin/env python3
"""Configure the pinned FFmpeg sources for the OpenOrbis target, without host probing."""
import argparse
import hashlib
from pathlib import Path
import os
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
for name in ("source", "prefix", "sdk", "sdk-include", "cc", "ld", "ar", "ranlib"):
    parser.add_argument("--" + name, required=True)
args = parser.parse_args()
source = Path(args.source).resolve()
build = Path(__file__).resolve().parent.parent / "build/ps4-x86_64"
if not source.is_relative_to(build) or not Path.cwd().resolve().is_relative_to(build):
    parser.error("FFmpeg source and working directory must be disposable PS4 build copies")
sdk = Path(args.sdk)
sdk_include = Path(args.sdk_include)
abi_hash = hashlib.sha256((sdk_include / "bits/alltypes.h").read_bytes()).hexdigest()
abi_stamp = Path(".nocturne-sdk-abi-sha256")
# Existing FFmpeg objects were compiled against the SDK's incompatible stat.
# A configure-step rerun alone does not invalidate every make object: clean
# this disposable out-of-source build when the overlay first appears/changes.
if Path("ffbuild/config.mak").exists() and (not abi_stamp.exists() or abi_stamp.read_text() != abi_hash):
    subprocess.run(["make", "clean"], check=True)
# OpenOrbis has a sysctl symbol stub but no supported header/API. Upstream's
# symbol-only probe incorrectly enables BSD CPU discovery. Require its header,
# as FFmpeg already does for sysctlbyname. Never patch the immutable cache.
configure = source / "configure"
original = configure.read_text()
needle = "check_func  sysctl\n"
replacement = "check_func_headers sys/sysctl.h sysctl\n"
if needle in original:
    configure.write_text(original.replace(needle, replacement))
elif replacement not in original:
    raise RuntimeError("Pinned FFmpeg sysctl probe was not found")
# FFmpeg's configure accepts shell fragments for compiler flags. Quote each
# component; subprocess itself always receives an argument vector.
cflags = shlex.join(["--target=x86_64-pc-freebsd12-elf", "--sysroot=" + str(sdk),
                     "-isystem", str(sdk_include),
                     "-isystem", str(sdk / "include"), "-fPIC", "-D_GNU_SOURCE", "-D__ORBIS__"])
ldflags = shlex.join(["-m", "elf_x86_64", "-pie", "--eh-frame-hdr", "-T", str(sdk / "link.x"),
                      "-L" + str(sdk / "lib"), str(sdk / "lib/crt1.o")])
env = os.environ.copy()
for key in ("CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "SDKROOT", "MACOSX_DEPLOYMENT_TARGET"):
    env.pop(key, None)
command = [str(Path(args.source) / "configure"), "--prefix=" + args.prefix,
    "--cc=" + shlex.quote(args.cc), "--ld=" + shlex.quote(args.ld),
    "--ar=" + shlex.quote(args.ar), "--ranlib=" + shlex.quote(args.ranlib),
    "--arch=x86_64", "--target-os=freebsd", "--enable-cross-compile",
    "--extra-cflags=" + cflags, "--extra-ldflags=" + ldflags, "--extra-libs=-lc -lkernel",
    "--enable-static", "--disable-shared", "--enable-pic", "--disable-programs", "--disable-doc",
    "--disable-network", "--disable-debug", "--disable-x86asm", "--disable-autodetect",
    "--disable-zlib", "--disable-bzlib", "--disable-lzma", "--disable-iconv", "--disable-libxcb",
    "--disable-sdl2", "--disable-avdevice", "--disable-avfilter", "--disable-postproc",
    "--disable-encoders", "--disable-muxers", "--disable-gpl", "--disable-nonfree"]
subprocess.run(command, check=True, env=env)
abi_stamp.write_text(abi_hash)
