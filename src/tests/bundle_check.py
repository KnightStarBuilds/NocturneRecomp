#!/usr/bin/env python3
"""Validate relocated app linkage without relying on the developer's library paths."""
from pathlib import Path
import os
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

app = Path(sys.argv[1])
root = Path(__file__).resolve().parents[2]
assert app.is_dir(), f"Build the app before running bundle checks: {app}"
with (app / "Contents/Info.plist").open("rb") as stream:
    metadata = plistlib.load(stream)
for key, expected in {
    "CFBundleExecutable": "NocturneRecompKnightStar",
    "CFBundleIdentifier": "org.nocturnerecompknightstar.game",
    "CFBundleName": "Nocturne Recomp (KnightStar)",
    "CFBundleDisplayName": "Nocturne Recomp (KnightStar)",
}.items():
    assert metadata.get(key) == expected, f"{key}: {metadata.get(key)!r}, expected {expected!r}"
version = metadata.get("CFBundleShortVersionString")
assert isinstance(version, str) and version, "Missing bundle version"
assert metadata.get("CFBundleVersion") == version, "Bundle version fields disagree"
# Launch the executable named by Finder's metadata, so a stale plist cannot
# pass merely because a separately hard-coded executable happens to exist.
executable_name = metadata["CFBundleExecutable"]
executable = app / "Contents/MacOS" / executable_name
assert executable.is_file() and os.access(executable, os.X_OK), executable
binaries = [executable, *sorted((app / "Contents/Frameworks").glob("*.dylib"))]
assert len(binaries) == 3, binaries
for binary in binaries:
    linked = subprocess.check_output(["/usr/bin/otool", "-L", str(binary)], text=True)
    for line in linked.splitlines()[1:]:
        dependency = line.strip().split(" (compatibility", 1)[0]
        assert dependency.startswith(("@rpath/", "/usr/lib/", "/System/Library/")), dependency
    commands = subprocess.check_output(["/usr/bin/otool", "-l", str(binary)], text=True)
    lines = commands.splitlines()
    for index, line in enumerate(lines):
        if line.strip() == "cmd LC_RPATH":
            path = lines[index + 2].strip().split(" (offset", 1)[0].removeprefix("path ")
            assert path.startswith(("@loader_path", "@executable_path")), path
    architectures = subprocess.check_output(["/usr/bin/lipo", "-archs", str(binary)], text=True).split()
    assert "arm64" in architectures, architectures
assert (app / "Contents/Resources/fonts/Fallback.ttf").stat().st_size > 10000
resources = app / "Contents/Resources"
# These tracked resources must follow source ownership changes into the app,
# including the distinct notices for original tooling and recovered code.
for source, bundled in (
    ("src/resources/gamecontrollerdb.txt", "gamecontrollerdb.txt"),
    ("src/resources/gamecontrollerdb.license.txt", "licenses/gamecontrollerdb-license.txt"),
    ("docs/NocturneDecomp-LICENSE", "licenses/NocturneDecomp-LICENSE"),
    ("docs/LICENSE.md", "licenses/LICENSE.md"),
):
    assert (resources / bundled).read_bytes() == (root / source).read_bytes(), bundled
for notice in (
    "sdl2-LICENSE.txt", "sdl2_ttf-LICENSE.txt",
    "sdl2_ttf-external-freetype-docs-FTL.TXT", "sdl2_ttf-external-harfbuzz-COPYING",
    "ffmpeg-COPYING.LGPLv2.1", "ffmpeg-LICENSE.md", "liberation-fonts-LICENSE",
):
    assert (resources / "licenses" / notice).stat().st_size > 0, notice
subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(app)], check=True)
with tempfile.TemporaryDirectory(prefix="nocturne relocated app ") as temporary:
    relocated = Path(temporary) / "App With Spaces.app"
    shutil.copytree(app, relocated, symlinks=True)
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("DYLD_", "NOCTURNE_"))}
    environment["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
    result = subprocess.run([str(relocated / "Contents/MacOS" / executable_name), "--version"],
                            cwd="/", env=environment, capture_output=True, text=True, check=True)
    expected = metadata["CFBundleDisplayName"] + " " + version
    assert re.match(re.escape(expected) + r"(?=[ ,\n]|$)", result.stdout), (
        f"Executable version disagrees with Info.plist: {result.stdout!r}, expected {expected!r}")
print("Bundle identity/version, relative linkage, ARM64, signatures, resources/notices and relocated launch without Homebrew paths passed")
