#!/usr/bin/env python3
"""Exercise PS4 script contracts with real offline preparation and fake compilers."""
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def snapshot(root):
    return {path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in root.rglob("*") if path.is_file()}


def executable(path, code):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("#!" + sys.executable + "\n" + code)
    path.chmod(0o755)


def archive(path, files):
    with tarfile.open(path, "w:gz") as stream:
        for name, content, mode in files:
            entry = tarfile.TarInfo("fixture/" + name)
            entry.size, entry.mode = len(content), mode
            stream.addfile(entry, io.BytesIO(content))
    return hashlib.sha256(path.read_bytes()).hexdigest()


class BuildTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="PS4 build tests ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve() / "project with spaces"
        (self.root / "ps4").mkdir(parents=True)
        (self.root / "deps/cache").mkdir(parents=True)
        for relative in ("ps4/build.sh", "ps4/build.py", "ps4/prepare_sdk.py", "deps/prepare.py"):
            shutil.copy2(ROOT / relative, self.root / relative)
        self.cache = self.root / "deps/cache"
        converter = ("#!" + sys.executable + "\nimport sys\nprint('Usage: create-fself test fixture')\n").encode()
        sources = {
            "shared": [("source.txt", b"immutable common source", 0o644)],
            "desktop-only": [("source.txt", b"immutable desktop source", 0o644)],
            "openorbis": [("PS4Toolchain/lib/crt1.o", b"crt fixture", 0o644),
                          ("PS4Toolchain/include/bits/alltypes.h", b"typedef unsigned int mode_t;\n", 0o644),
                          ("PS4Toolchain/bin/macos/create-fself-macos", converter, 0o755),
                          ("PS4Toolchain/bin/linux/create-fself", converter, 0o755)],
        }
        self.dependencies = []
        for name, files in sources.items():
            entry = {"name": name, "version": "fixture", "archive": name + ".tar.gz",
                     "sha256": archive(self.cache / (name + ".tar.gz"), files),
                     "url": "https://invalid.example/network-must-not-be-used"}
            if name != "shared":
                entry["targets"] = ["ps4" if name == "openorbis" else "desktop"]
            self.dependencies.append(entry)
        (self.root / "deps/manifest.json").write_text(json.dumps({"dependencies": self.dependencies}))
        self.tools = self.root / "fake tools"
        self.calls = self.root / "calls.jsonl"
        log = ("import json, os, sys\nfrom pathlib import Path\n"
               "with open(os.environ['PS4_TEST_CALLS'], 'a') as out:\n"
               "    out.write(json.dumps({'tool': Path(sys.argv[0]).name, 'args': sys.argv[1:], "
               "'env': {key: os.environ.get(key) for key in ['CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'SDKROOT']}}) + '\\n')\n")
        for name in ("clang", "clang++", "ld.lld", "llvm-ar", "llvm-ranlib", "make", "ninja"):
            executable(self.tools / name, log + "print('clang version 18.1.8')\n")
        executable(self.tools / "cmake", log + "print('cmake version 3.24.4')\n")
        executable(self.tools / "curl", "raise SystemExit('Offline test attempted a download')\n")
        (self.tools / "python3").symlink_to(sys.executable)
        (self.tools / "bash").symlink_to(shutil.which("bash"))
        (self.tools / "dirname").symlink_to(shutil.which("dirname"))
        self.environment = os.environ.copy()
        self.environment.pop("OO_PS4_TOOLCHAIN", None)
        self.environment.update({"PATH": str(self.tools),
                                 "PS4_LLVM_DIR": str(self.tools), "PS4_TEST_CALLS": str(self.calls),
                                 "NOCTURNE_OFFLINE": "1", "CMAKE_BUILD_PARALLEL_LEVEL": "2",
                                 "CC": "unwanted host compiler", "CXX": "unwanted host compiler",
                                 "CFLAGS": "unwanted flags", "CXXFLAGS": "unwanted flags",
                                 "CPPFLAGS": "unwanted flags", "LDFLAGS": "unwanted flags", "SDKROOT": "host SDK"})
        self.build = self.root / "build/ps4-x86_64"
        self.build.mkdir(parents=True)
        self.sentinel = self.build / "previous successful output"
        self.sentinel.write_text("preserve me until preflight succeeds")
        for relative in ("build/macos-arm64/desktop.keep", "data/original/ACT1.POD", "src/game/fixture.cpp"):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("untouched fixture")

    def run_build(self, *arguments, expected=0):
        result = subprocess.run([str(self.root / "ps4/build.sh"), *arguments], cwd="/",
                                env=self.environment, capture_output=True, text=True)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def recorded_calls(self):
        return [json.loads(line) for line in self.calls.read_text().splitlines()]

    def test_invalid_arguments_do_not_mutate_project(self):
        before = snapshot(self.root)
        for arguments in (("--clean", "--unknown"), ("--clean", "--config=Invalid"), ("--config",), ("--run",)):
            with self.subTest(arguments=arguments):
                self.run_build(*arguments, expected=2)
                self.assertEqual(snapshot(self.root), before)
        self.run_build("--help")
        self.assertEqual(snapshot(self.root), before)

    def test_missing_tool_preserves_build_before_clean(self):
        (self.tools / "clang++").unlink()
        result = self.run_build("--clean", expected=1)
        self.assertIn("Required tool not found", result.stderr)
        self.assertTrue(self.sentinel.exists())
        self.assertFalse((self.cache / "openorbis").exists())

    def test_offline_missing_archive_preserves_build_before_clean(self):
        (self.cache / "openorbis.tar.gz").unlink()
        result = self.run_build("--clean", expected=1)
        self.assertIn("Offline: missing", result.stderr)
        self.assertTrue(self.sentinel.exists())

    def test_default_release_clean_and_cache_preservation(self):
        protected_paths = (self.root / "data", self.root / "src", self.root / "build/macos-arm64")
        protected = [snapshot(path) for path in protected_paths]
        self.run_build()
        configure = [call for call in self.recorded_calls() if call["tool"] == "cmake" and call["args"][0] == "-S"][-1]
        self.assertIn("Ninja Multi-Config", configure["args"])
        self.assertEqual(configure["args"][:2], ["-S", str(self.root)])
        self.assertTrue(all(value is None for value in configure["env"].values()))
        build_call = [call for call in self.recorded_calls() if call["tool"] == "cmake"][-1]
        self.assertEqual(build_call["args"], ["--build", str(self.build), "--config", "Debug", "--parallel", "2"])
        self.assertTrue(self.sentinel.exists())
        self.assertFalse((self.cache / "desktop-only").exists())
        self.assertFalse((self.build / "dependency-sources/desktop-only").exists())
        cache_before = snapshot(self.cache)
        stamp_time = (self.build / ".shared-prepared").stat().st_mtime_ns
        self.run_build("--config=Release")
        self.assertEqual((self.build / ".shared-prepared").stat().st_mtime_ns, stamp_time)
        build_call = [call for call in self.recorded_calls() if call["tool"] == "cmake"][-1]
        self.assertEqual(build_call["args"][3], "Release")
        self.run_build("--clean")
        self.assertFalse(self.sentinel.exists())
        self.assertEqual(snapshot(self.cache), cache_before)
        self.assertEqual([snapshot(path) for path in protected_paths], protected)
        self.assertTrue((self.build / "dependency-sources/openorbis/PS4Toolchain/lib/crt1.o").is_file())

    def test_default_preparation_keeps_desktop_target(self):
        desktop = self.root / "build/desktop-fixture"
        result = subprocess.run([sys.executable, str(self.root / "deps/prepare.py"), "--build-dir", str(desktop), "--offline"],
                                cwd="/", env=self.environment, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((desktop / "dependency-sources/desktop-only/source.txt").is_file())
        self.assertTrue((desktop / "dependency-sources/shared/source.txt").is_file())
        self.assertFalse((desktop / "dependency-sources/openorbis").exists())
        self.assertFalse((self.cache / "openorbis").exists())

    def test_changed_immutable_cache_preserves_build(self):
        self.run_build()
        (self.cache / "shared/source.txt").write_text("untracked mutation")
        result = self.run_build("--clean", expected=1)
        self.assertIn("Immutable source cache changed", result.stderr)
        self.assertTrue(self.sentinel.exists())


if __name__ == "__main__":
    unittest.main()
