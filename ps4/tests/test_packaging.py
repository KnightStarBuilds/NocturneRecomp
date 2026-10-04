#!/usr/bin/env python3
"""Isolated packaging integration checks; these do not prove console playability."""

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

PS4 = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("nocturne_ps4_package", PS4 / "package.py")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


def elf_header(kind):
    result = bytearray(64)
    result[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HH", result, 16, kind, 62)
    struct.pack_into("<H", result, 52, 64)
    return bytes(result)


FAKE_TOOL = r'''#!/usr/bin/env python3
from pathlib import Path
import json
import sys
import xml.etree.ElementTree as ET
mode = Path(__file__).with_suffix('.mode').read_text().strip()
if sys.argv[1] == 'version':
    if mode == 'version-fail':
        raise SystemExit(7)
    print('PkgTool test fixture; LibOrbisPkg test fixture')
elif sys.argv[1] == 'pkg_build':
    if mode == 'build-fail':
        print('injected build failure')
        raise SystemExit(4)
    tree = ET.parse(sys.argv[2])
    content_id = tree.find('./volume/package').get('content_id')
    files = {node.get('targ_path'): Path(node.get('orig_path')).read_bytes().hex()
             for node in tree.findall('./files/file')}
    dirs = [node.get('targ_name') for node in tree.findall('.//rootdir//dir')]
    if mode != 'missing-output':
        (Path(sys.argv[3]) / (content_id + '.pkg')).write_bytes(
            b'\x7fCNT' + json.dumps({'files': files, 'dirs': dirs}).encode())
elif sys.argv[1] == 'pkg_validate':
    if mode == 'validation-error-zero-exit':
        print('[ERROR] Hash invalid at 0x100: fixture')
    elif mode == 'validation-empty':
        print('no validations')
    elif mode == 'validation-fail':
        raise SystemExit(9)
    else:
        print('[OK] (Hash) fixture @ 0x100')
        print('[WARN] Signature cannot be validated without secret keys')
else:
    raise SystemExit(2)
'''


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="Nocturne PS4 test & ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve() / "repo ' with spaces"
        (self.root / "ps4/resources").mkdir(parents=True)
        for relative in ("package.json", "resources/icon0.png", "resources/icon-license.txt"):
            shutil.copyfile(PS4 / relative, self.root / "ps4" / relative)
        self.build = self.root / "build/ps4-x86_64"
        self.bin = self.build / "bin/Debug"
        self.bin.mkdir(parents=True)
        (self.bin / "nocturne.elf").write_bytes(elf_header(3))
        self_header = bytearray(64)
        self_header[:4] = b"\x4f\x15\x3d\x1d"
        struct.pack_into("<Q", self_header, 16, 128)
        struct.pack_into("<H", self_header, 24, 1)
        (self.bin / "eboot.bin").write_bytes(self_header + elf_header(0xFE10))
        self.resources = self.build / "resources/Debug"
        (self.resources / "fonts").mkdir(parents=True)
        (self.resources / "licenses").mkdir()
        (self.resources / "sce_module").mkdir()
        (self.resources / "fonts/Fallback.ttf").write_bytes(b"font fixture")
        (self.resources / "gamecontrollerdb.txt").write_bytes(b"controller fixture")
        (self.resources / "licenses/library.txt").write_bytes(b"license fixture")
        for module in ("libc.prx", "libSceFios2.prx"):
            (self.resources / "sce_module" / module).write_bytes(module.encode() + b" stub fixture\0\xff")
        self.data = self.root / "original data & ' archives"
        (self.data / "movies & scenes").mkdir(parents=True)
        (self.data / "STARTUP.POD").write_bytes(b"POD fixture\0\xff")
        (self.data / "mission.pod").write_bytes(b"POD mission")
        (self.data / "POD.INI").write_bytes(b"mission.pod\r\nSTARTUP.POD\r\n")
        (self.data / "movies & scenes/intro.mov").write_bytes(b"movie fixture")
        (self.data / "empty.txt").touch()
        (self.data / ".DS_Store").write_bytes(b"Finder metadata")
        self.tool = Path(self.temporary.name) / "PkgTool fixture"
        self.tool.write_text(FAKE_TOOL)
        self.tool.chmod(0o755)
        self.mode("success")
        self.environment = {"PS4_PKG_TOOL": str(self.tool)}
        metadata = json.loads((self.root / "ps4/package.json").read_text())
        self.output = self.build / "pkg/Debug" / (metadata["content_id"] + ".pkg")

    def mode(self, value):
        self.tool.with_suffix(".mode").write_text(value)

    def package(self, config="Debug"):
        with contextlib.redirect_stdout(io.StringIO()):
            return PACKAGE.package(self.root, config, self.data, self.environment)

    def stage(self, config="Debug"):
        with contextlib.redirect_stdout(io.StringIO()):
            return PACKAGE.stage_app0(self.root, config, self.data)

    @staticmethod
    def tree_files(directory):
        return {path.relative_to(directory).as_posix(): path.read_bytes()
                for path in directory.rglob("*") if path.is_file()}

    def sfo_entries(self, sfo):
        magic, version, keys, values, count = struct.unpack_from("<4sIIII", sfo)
        self.assertEqual((magic, version, values % 4), (b"\0PSF", 0x101, 0))
        entries = {}
        for index in range(count):
            key_offset, kind, length, capacity, data_offset = struct.unpack_from("<HHIII", sfo, 20 + index * 16)
            key = sfo[keys + key_offset:].split(b"\0", 1)[0].decode()
            value = sfo[values + data_offset:values + data_offset + length]
            self.assertLessEqual(length, capacity)
            self.assertEqual(data_offset % 4, 0)
            entries[key] = value.rstrip(b"\0").decode() if kind == 0x204 else struct.unpack("<I", value)[0]
        return entries

    def assert_preflight_failure(self, text):
        with self.assertRaisesRegex(PACKAGE.PackageError, text):
            self.package()
        self.assertFalse((self.build / "stage").exists())
        self.assertFalse((self.build / "pkg").exists())

    def test_layout_bytes_manifest_and_special_paths_survive(self):
        nested_pod = self.data / "movies & scenes/EXTRA.POD"
        nested_pod.write_bytes(b"nested archive remains in place")
        self.assertEqual(self.package(), self.output)
        result = json.loads(self.output.read_bytes()[4:])
        files = {name: bytes.fromhex(value) for name, value in result["files"].items()}
        self.assertEqual(files["data/original/POD.INI"], (self.data / "POD.INI").read_bytes())
        self.assertEqual(files["data/original/STARTUP.POD"], (self.data / "STARTUP.POD").read_bytes())
        self.assertIn("data/original/movies & scenes/intro.mov", files)
        self.assertEqual(files["data/original/movies & scenes/EXTRA.POD"], nested_pod.read_bytes())
        self.assertIn("movies & scenes", result["dirs"])
        self.assertEqual(files["data/original/empty.txt"], b"")
        self.assertNotIn("data/original/.DS_Store", files)
        self.assertIn("fonts/Fallback.ttf", files)
        self.assertIn("gamecontrollerdb.txt", files)
        self.assertIn("licenses/library.txt", files)
        self.assertIn("sce_module", result["dirs"])
        for module in ("libc.prx", "libSceFios2.prx"):
            target = "sce_module/" + module
            self.assertEqual(files[target], (self.resources / target).read_bytes())
        manifest = json.loads(files["package-manifest.json"])
        self.assertEqual(manifest["configuration"], "Debug")
        self.assertEqual(manifest["files"]["data/original/STARTUP.POD"]["sha256"], PACKAGE.sha256(self.data / "STARTUP.POD"))
        self.assertEqual(list((self.build / "stage/Debug").iterdir()), [])

    def test_release_uses_its_own_build(self):
        shutil.copytree(self.bin, self.build / "bin/Release")
        shutil.copytree(self.resources, self.build / "resources/Release")
        output = self.package("Release")
        self.assertEqual(output.parent.name, "Release")
        self.assertFalse(self.output.exists())

    def test_stage_matches_package_files_without_pkgtool(self):
        self.package()
        packaged = json.loads(self.output.read_bytes()[4:])
        expected = {name: bytes.fromhex(value) for name, value in packaged["files"].items()}
        protected = [self.tree_files(path) for path in (self.data, self.resources, self.bin)]
        previous_pkg = self.output.read_bytes()
        with mock.patch.object(PACKAGE, "select_tool", side_effect=AssertionError("staging selected PkgTool")), \
             mock.patch.object(PACKAGE.subprocess, "run", side_effect=AssertionError("staging ran a subprocess")):
            output = self.stage()
        self.assertEqual(output, self.build / "app0/Debug")
        actual = self.tree_files(output)
        manifest = json.loads(actual.pop("package-manifest.json"))
        package_manifest = json.loads(expected.pop("package-manifest.json"))
        self.assertEqual(actual, expected)
        self.assertIsNone(manifest["packager"])
        package_manifest["packager"] = None
        self.assertEqual(manifest, package_manifest)
        for target, details in manifest["files"].items():
            self.assertEqual(details["sha256"], PACKAGE.sha256(output / target))
            self.assertEqual(details["size"], (output / target).stat().st_size)
        self.assertEqual(self.output.read_bytes(), previous_pkg)
        self.assertEqual([self.tree_files(path) for path in (self.data, self.resources, self.bin)], protected)
        self.assertEqual(list((self.build / "stage/Debug").iterdir()), [])

    def test_stage_refresh_removes_stale_files_and_preserves_other_config(self):
        debug = self.stage()
        shutil.copytree(self.bin, self.build / "bin/Release")
        shutil.copytree(self.resources, self.build / "resources/Release")
        release_eboot = self.build / "bin/Release/eboot.bin"
        release_eboot.write_bytes(release_eboot.read_bytes() + b"release executable")
        (self.build / "resources/Release/fonts/Fallback.ttf").write_bytes(b"release font")
        debug_before = self.tree_files(debug)
        release = self.stage("Release")
        self.assertEqual(release, self.build / "app0/Release")
        self.assertEqual(self.tree_files(debug), debug_before)
        self.assertEqual((release / "eboot.bin").read_bytes(), release_eboot.read_bytes())
        self.assertEqual((release / "fonts/Fallback.ttf").read_bytes(), b"release font")
        self.assertEqual(json.loads((release / "package-manifest.json").read_text())["configuration"], "Release")
        release_before = self.tree_files(release)
        (debug / "stale-file").write_bytes(b"remove when refreshing")
        (self.data / "mission.pod").write_bytes(b"updated archive")
        self.stage()
        self.assertFalse((debug / "stale-file").exists())
        self.assertEqual((debug / "data/original/mission.pod").read_bytes(), b"updated archive")
        self.assertEqual(self.tree_files(release), release_before)

    def test_stage_validation_failure_preserves_previous_outputs(self):
        output = self.stage()
        previous = self.tree_files(output)
        self.output.parent.mkdir(parents=True)
        self.output.write_bytes(b"previous package")
        for relative in ("fonts/Fallback.ttf", "sce_module/libc.prx", "sce_module/libSceFios2.prx"):
            path = self.resources / relative
            contents = path.read_bytes()
            path.unlink()
            try:
                with self.assertRaises(PACKAGE.PackageError):
                    self.stage()
                self.assertEqual(self.tree_files(output), previous)
                self.assertEqual(self.output.read_bytes(), b"previous package")
            finally:
                path.write_bytes(contents)
        (self.resources / "DATA").mkdir()
        (self.resources / "DATA/collision").write_bytes(b"ambiguous directory")
        with self.assertRaisesRegex(PACKAGE.PackageError, "case-ambiguous"):
            self.stage()
        self.assertEqual(self.tree_files(output), previous)
        self.assertEqual(list((self.build / "stage/Debug").iterdir()), [])

    def test_stage_partial_copy_failure_preserves_previous_tree(self):
        output = self.stage()
        previous = self.tree_files(output)
        original_copy = shutil.copyfile
        copied = []

        def fail_copy(source, destination):
            if source.name == "gamecontrollerdb.txt":
                raise OSError("injected copy failure")
            copied.append(source)
            return original_copy(source, destination)

        with mock.patch.object(PACKAGE.shutil, "copyfile", side_effect=fail_copy):
            with self.assertRaisesRegex(OSError, "injected copy failure"):
                self.stage()
        self.assertTrue(copied)
        self.assertEqual(self.tree_files(output), previous)
        self.assertEqual(list((self.build / "stage/Debug").iterdir()), [])

    def test_stage_promotion_failure_restores_previous_tree(self):
        output = self.stage()
        previous = self.tree_files(output)
        original_replace = os.replace

        def fail_promotion(source, destination):
            if source.name == "app0":
                raise OSError("injected promotion failure")
            return original_replace(source, destination)

        with mock.patch.object(PACKAGE.os, "replace", side_effect=fail_promotion):
            with self.assertRaisesRegex(OSError, "injected promotion failure"):
                self.stage()
        self.assertEqual(self.tree_files(output), previous)
        self.assertEqual(list((self.build / "stage/Debug").iterdir()), [])

    def test_stage_interrupt_after_backup_rename_restores_previous_tree(self):
        output = self.stage()
        previous = self.tree_files(output)
        original_replace = os.replace

        def interrupt_after_backup(source, destination):
            original_replace(source, destination)
            if source == output:
                raise KeyboardInterrupt()

        with mock.patch.object(PACKAGE.os, "replace", side_effect=interrupt_after_backup):
            with self.assertRaises(KeyboardInterrupt):
                self.stage()
        self.assertEqual(self.tree_files(output), previous)
        self.assertEqual(list((self.build / "stage/Debug").iterdir()), [])

    def test_stage_failed_restore_retains_backup_and_reports_its_path(self):
        output = self.stage()
        previous = self.tree_files(output)
        original_replace = os.replace

        def fail_publication_and_restore(source, destination):
            if destination == output:
                raise OSError("injected publication/restore failure")
            return original_replace(source, destination)

        with mock.patch.object(PACKAGE.os, "replace", side_effect=fail_publication_and_restore):
            with self.assertRaises(PACKAGE.PackageError) as failure:
                self.stage()
        backups = list((self.build / "stage/Debug").glob("app0-*/previous-app0"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(self.tree_files(backups[0]), previous)
        self.assertIn(str(backups[0]), str(failure.exception))

    def test_stage_rejects_output_symlinks_and_asset_overlap(self):
        output = self.stage()
        previous = self.tree_files(output)
        for linked in (output, output.parent):
            with self.subTest(symlink=linked):
                saved = self.root / "saved output"
                linked.rename(saved)
                linked.symlink_to(saved, target_is_directory=True)
                try:
                    with self.assertRaisesRegex(PACKAGE.PackageError, "symlinks"):
                        self.stage()
                finally:
                    linked.unlink()
                    saved.rename(linked)
                self.assertEqual(self.tree_files(output), previous)
        original_data = self.data
        self.data = output / "data/original"
        with self.assertRaisesRegex(PACKAGE.PackageError, "overlap"):
            self.stage()
        self.assertEqual(self.tree_files(output), previous)
        self.data = self.root
        (self.root / "fixture.pod").write_bytes(b"archive in an ancestor of output")
        with self.assertRaisesRegex(PACKAGE.PackageError, "overlap"):
            self.stage()
        self.data = original_data
        self.assertEqual(self.tree_files(output), previous)

    def test_stage_cli_defaults_and_explicit_release_with_spaces(self):
        shutil.copyfile(PS4 / "package.py", self.root / "ps4/package.py")
        shutil.copytree(self.bin, self.build / "bin/Release")
        shutil.copytree(self.resources, self.build / "resources/Release")
        environment = dict(os.environ, PS4_PKG_TOOL=str(self.root / "missing tool"))
        for config, arguments in (("Debug", []), ("Release", ["--config=Release"])):
            result = subprocess.run([sys.executable, str(self.root / "ps4/package.py"),
                                     "--stage-only", "--data-dir", str(self.data), *arguments],
                                    cwd="/", env=environment, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue((self.build / "app0" / config / "eboot.bin").is_file())
        self.assertFalse((self.build / "pkg").exists())

    def test_all_tool_failures_preserve_previous_package(self):
        self.output.parent.mkdir(parents=True)
        self.output.write_bytes(b"previous successful package")
        for mode in ("version-fail", "build-fail", "missing-output", "validation-error-zero-exit", "validation-empty", "validation-fail"):
            with self.subTest(mode=mode):
                self.mode(mode)
                with self.assertRaises(PACKAGE.PackageError):
                    self.package()
                self.assertEqual(self.output.read_bytes(), b"previous successful package")

    def test_invalid_metadata_does_not_create_staging(self):
        path = self.root / "ps4/package.json"
        metadata = json.loads(path.read_text())
        metadata["content_id"] = "IV0000-NCTR00001_00-NOCTURNEKNIGHT000"
        path.write_text(json.dumps(metadata))
        self.assert_preflight_failure("content_id")

    def test_missing_build_does_not_create_staging(self):
        (self.bin / "eboot.bin").unlink()
        self.assert_preflight_failure("Required regular file")

    def test_desktop_executable_does_not_create_staging(self):
        (self.bin / "eboot.bin").write_bytes(elf_header(3))
        self.assert_preflight_failure("Not a PS4 SELF")

    def test_desktop_elf_inside_self_is_rejected(self):
        eboot = self.bin / "eboot.bin"
        eboot.write_bytes(eboot.read_bytes()[:64] + elf_header(3))
        self.assert_preflight_failure("Not the expected x86-64 PS4")

    def test_truncated_self_is_rejected(self):
        eboot = self.bin / "eboot.bin"
        eboot.write_bytes(eboot.read_bytes()[:90])
        self.assert_preflight_failure("Truncated or invalid")

    def test_missing_and_empty_pod_are_rejected(self):
        (self.data / "STARTUP.POD").unlink()
        (self.data / "mission.pod").unlink()
        self.assert_preflight_failure("No POD archives")
        (self.data / "empty.pod").touch()
        self.assert_preflight_failure("Required file is empty")

    def test_nested_only_pods_fail_before_mutation(self):
        for name in ("STARTUP.POD", "mission.pod"):
            (self.data / name).rename(self.data / "movies & scenes" / name)
        self.output.parent.mkdir(parents=True)
        self.output.write_bytes(b"previous successful package")
        before = {path.relative_to(self.root): path.read_bytes()
                  for path in self.root.rglob("*") if path.is_file()}
        with self.assertRaisesRegex(PACKAGE.PackageError, "No POD archives found directly"):
            self.package()
        self.assertFalse((self.build / "stage").exists())
        after = {path.relative_to(self.root): path.read_bytes()
                 for path in self.root.rglob("*") if path.is_file()}
        self.assertEqual(after, before)

    def test_symlink_data_is_rejected(self):
        (self.data / "external").symlink_to(self.resources, target_is_directory=True)
        self.assert_preflight_failure("regular files/directories")

    def test_missing_resource_is_rejected(self):
        (self.resources / "fonts/Fallback.ttf").unlink()
        self.assert_preflight_failure("Fallback.ttf")

    def test_missing_or_empty_runtime_module_preserves_previous_package(self):
        self.output.parent.mkdir(parents=True)
        self.output.write_bytes(b"previous successful package")
        for module in ("libc.prx", "libSceFios2.prx"):
            path = self.resources / "sce_module" / module
            contents = path.read_bytes()
            for state in ("missing", "empty"):
                with self.subTest(module=module, state=state):
                    if state == "missing":
                        path.unlink()
                    else:
                        path.write_bytes(b"")
                    with self.assertRaises(PACKAGE.PackageError) as failure:
                        self.package()
                    self.assertIn(str(path), str(failure.exception))
                    self.assertFalse((self.build / "stage").exists())
                    self.assertEqual(self.output.read_bytes(), b"previous successful package")
                    path.write_bytes(contents)

    def test_resource_cannot_replace_executable(self):
        (self.resources / "eboot.bin").write_bytes(b"unexpected executable")
        self.assert_preflight_failure("Duplicate")

    def test_case_ambiguous_directories_are_rejected(self):
        (self.resources / "DATA").mkdir()
        (self.resources / "DATA/extra.bin").write_bytes(b"case collision")
        self.assert_preflight_failure("case-ambiguous")

    def test_file_cannot_shadow_a_package_directory(self):
        (self.resources / "data").write_bytes(b"file shadows original-data directory")
        self.assert_preflight_failure("Duplicate")

    def test_missing_tool_is_rejected(self):
        self.environment["PS4_PKG_TOOL"] = str(self.root / "missing tool")
        self.assert_preflight_failure("missing or not executable")

    def test_relative_tool_is_rejected(self):
        self.environment["PS4_PKG_TOOL"] = "PkgTool.Core"
        self.assert_preflight_failure("absolute executable")

    def test_invalid_arguments_fail_before_accessing_build(self):
        shutil.copyfile(PS4 / "package.py", self.root / "ps4/package.py")
        for args in (["--config=RelWithDebInfo"], ["--clean"], ["--config"], ["--data-dir"]):
            with self.subTest(args=args):
                result = subprocess.run([sys.executable, str(self.root / "ps4/package.py"), *args], capture_output=True)
                self.assertEqual(result.returncode, 2)
                self.assertFalse((self.build / "stage").exists())

    def test_sfo_has_aligned_values_and_stable_identifiers(self):
        metadata = PACKAGE.read_metadata(self.root / "ps4/package.json")
        entries = self.sfo_entries(PACKAGE.encode_sfo(metadata))
        self.assertEqual(entries["CONTENT_ID"], metadata["content_id"])
        self.assertEqual(entries["TITLE_ID"], "NCTR00001")
        self.assertEqual(entries["CATEGORY"], "gd")
        self.assertEqual(entries["SYSTEM_VER"], 0x09000000)

    def test_package_and_app0_use_cross_to_confirm_native_dialogs(self):
        shutil.copytree(self.bin, self.build / "bin/Release")
        shutil.copytree(self.resources, self.build / "resources/Release")
        for config in ("Debug", "Release"):
            output = self.package(config)
            packaged = json.loads(output.read_bytes()[4:])
            app0 = self.stage(config)
            sfos = {
                "package": bytes.fromhex(packaged["files"]["sce_sys/param.sfo"]),
                "app0": (app0 / "sce_sys/param.sfo").read_bytes(),
            }
            for target, sfo in sfos.items():
                with self.subTest(configuration=config, target=target):
                    # SFO bit 1 makes native dialogs match the game's Cross confirm.
                    self.assertEqual(self.sfo_entries(sfo)["ATTRIBUTE"], 0x2)

    def test_preserved_runtime_notices_match_pinned_sources(self):
        root = PS4.parent
        notices = json.loads((root / "deps/ps4-notices.json").read_text())
        self.assertGreaterEqual(len(notices["notices"]), 5)
        for notice in notices["notices"]:
            with self.subTest(path=notice["path"]):
                self.assertEqual(PACKAGE.sha256(root / notice["path"]), notice["sha256"])


if __name__ == "__main__":
    unittest.main()
