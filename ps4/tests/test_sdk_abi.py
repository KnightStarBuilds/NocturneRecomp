#!/usr/bin/env python3
"""Check immutable SDK overlays and, when available, the real target stat ABI."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("ps4_prepare_sdk", ROOT / "ps4/prepare_sdk.py")
SDK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SDK)


class SDKOverlayTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="PS4 SDK ABI ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.sdk = self.root / "immutable SDK"
        self.header = self.sdk / "include/bits/alltypes.h"
        self.header.parent.mkdir(parents=True)
        self.original = "// Original upstream notice\ntypedef unsigned int mode_t;\n// Older correct declaration\ntypedef unsigned short mode_t;\n"
        self.header.write_text(self.original)
        self.output = self.root / "generated overlay"

    def test_overlay_preserves_sdk_and_other_declarations(self):
        SDK.prepare(self.sdk, self.output)
        self.assertEqual(self.header.read_text(), self.original)
        generated = self.output / "bits/alltypes.h"
        self.assertEqual(generated.read_text(), self.original.replace("unsigned int mode_t", "unsigned short mode_t"))
        modified = generated.stat().st_mtime_ns
        SDK.prepare(self.sdk, self.output)
        self.assertEqual(generated.stat().st_mtime_ns, modified)

    def test_check_only_never_creates_overlay(self):
        SDK.prepare(self.sdk, self.output, check_only=True)
        self.assertFalse(self.output.exists())
        self.assertEqual(self.header.read_text(), self.original)

    def test_unknown_sdk_and_output_inside_sdk_are_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "outside the immutable"):
            SDK.prepare(self.sdk, self.sdk / "generated")
        self.assertFalse((self.sdk / "generated").exists())
        self.header.write_text("typedef unsigned long mode_t;\n")
        with self.assertRaisesRegex(RuntimeError, "Unrecognized"):
            SDK.prepare(self.sdk, self.output)
        self.assertFalse(self.output.exists())

    def test_actual_ps4_native_stat_layout(self):
        sdk = ROOT / "build/ps4-x86_64/dependency-sources/openorbis/PS4Toolchain"
        if not (sdk / "include/bits/alltypes.h").is_file():
            self.skipTest("Pinned PS4 SDK has not been prepared")
        compiler = shutil.which("clang++")
        if not compiler:
            self.skipTest("Clang target compiler is unavailable")
        SDK.prepare(sdk, self.output)
        command = [compiler, "--target=x86_64-pc-freebsd12-elf", "--sysroot=" + str(sdk),
                   "-nostdinc++", "-isystem", str(sdk / "include/c++/v1"),
                   "-isystem", str(self.output), "-isystem", str(sdk / "include"),
                   "-std=c++23", "-fsyntax-only", str(ROOT / "ps4/runtime/sdk_abi.cpp")]
        environment = os.environ.copy()
        for key in ("SDKROOT", "MACOSX_DEPLOYMENT_TARGET", "CPATH", "CPLUS_INCLUDE_PATH"):
            environment.pop(key, None)
        result = subprocess.run(command, env=environment, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        # The unmodified pinned header must exercise the guard, rather than a
        # test that succeeds regardless of which SDK include directory wins.
        if "typedef unsigned int mode_t;" in (sdk / "include/bits/alltypes.h").read_text():
            without_overlay = command.copy()
            index = without_overlay.index(str(self.output))
            del without_overlay[index - 1:index + 1]
            result = subprocess.run(without_overlay, env=environment, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("static assertion failed", result.stderr)


if __name__ == "__main__":
    unittest.main()
