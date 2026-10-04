#!/usr/bin/env python3
"""Exercise the maintained emulator workflow without launching an emulator."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import signal
import stat
import subprocess
import sys
import unittest
from unittest import mock

PS4 = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


VALIDATE = module("nocturne_ps4_validation", PS4 / "validate.py")
FIXTURES = module("nocturne_packaging_fixtures", PS4 / "tests/test_packaging.py")


class FakeProcess:
    pid = 43210

    def __init__(self, test, command, **kwargs):
        self.test, self.command, self.options = test, command, kwargs
        self.mode, self.returncode = test.mode, None
        self.terminated = self.killed = False
        self.waits = 0
        kwargs["stdout"].write(b"fake emulator output\n")
        self.output = Path(kwargs["cwd"])

    def wait(self, timeout):
        self.waits += 1
        if self.returncode is not None:
            return self.returncode
        running = json.loads((self.output / "result.json").read_text())
        self.test.assertEqual(running["status"], "running")
        self.test.assertEqual(running["pid"], self.pid)
        self.test.assertEqual(running["launcher_command"], self.command)
        if self.mode in ("timeout", "kill"):
            raise subprocess.TimeoutExpired(self.command, timeout)
        if self.mode == "interrupted":
            raise VALIDATE.InterruptedRun(signal.SIGTERM)
        profile = self.output / "user/data/NocturneRecompKnightStar"
        (profile / "startup.log").write_text("Nocturne PS4: beginning runtime preparation\n")
        (profile / "stderr.txt").write_text("mci open 'opening.avi'\nmci drained after 1000ms\n")
        (profile / "save").mkdir(exist_ok=True)
        (profile / "save/new.noc").write_bytes(b"fake save")
        captures = self.output / "user/data/captures"
        if self.mode != "missing":
            count = 1 if self.mode == "partial" else 2
            events = "elapsed_ms,event,detail\n"
            for number in range(count):
                name = f"frame-{number:06}-ms-{number:04}.ppm"
                data = b"P6\n1 1\n255\n\n\x00\xff"
                if self.mode == "corrupt" and number == 1:
                    data = data[:-1]
                (captures / name).write_bytes(data)
                events += f"{number * 500},capture,{name}\n"
            if self.mode != "no-keys":
                events += "200,key,Return\n" if self.mode == "wrong-key" else "200,key,Escape\n"
            if self.mode == "extra-key":
                events += "400,key,Escape\n"
            if self.mode != "no-quit":
                events += "1000,quit,requested\n"
            (captures / "events.csv").write_text(events)
            (captures / "movie-audio.csv").write_text("elapsed_ms,rate,channels,samples,nonzero,peak,rms\n0,44100,2,8,4,100,20\n")
        self.returncode = 7 if self.mode == "nonzero" else 0
        return self.returncode

    def poll(self):
        return self.returncode

    def terminate(self):
        self.terminated = True
        if self.mode != "kill":
            self.returncode = 0

    def kill(self):
        self.killed = True
        self.returncode = -9


@unittest.skipUnless(sys.platform in ("darwin", "linux"), "validator launch supports macOS and Linux")
class ValidationTests(unittest.TestCase):
    def setUp(self):
        # Reuse the established synthetic ELF/SELF/assets fixture, not a real
        # build tree, package tool, game profile or emulator executable.
        self.fixture = FIXTURES.PackagingTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.root, self.data = self.fixture.root, self.fixture.data
        scenario_file = self.root / "src/tests/smoke-scenarios.json"
        scenario_file.parent.mkdir(parents=True)
        scenario_file.write_text(json.dumps({
            "menu": {"seconds": 1, "capture_at": "0,0.5", "keys": "0.2:Escape"},
            "cursor": {"seconds": 1, "capture_at": "0,0.5", "keys": "", "mouse_at": "0:5:5"}}))
        self.emulator = self.root / "fake shadPS4 executable"
        self.emulator.write_bytes(b"this fixture is never executed")
        self.emulator.chmod(0o755)
        self.output = self.root / "capture output"
        self.mode = "success"
        self.processes = []

    def popen(self, command, **kwargs):
        process = FakeProcess(self, command, **kwargs)
        self.processes.append(process)
        return process

    def killpg(self, pid, signum):
        self.assertEqual(pid, self.processes[-1].pid)
        if signum == signal.SIGTERM:
            self.processes[-1].terminate()
        elif signum == signal.SIGKILL:
            self.processes[-1].kill()
        else:
            self.fail("unexpected process-group signal")

    def run_validation(self, **kwargs):
        kwargs.setdefault("data_directory", self.data)
        kwargs.setdefault("environment", {"HOME": "/unchanged/home", "SDL_AUDIODRIVER": "dummy"})
        with mock.patch.object(VALIDATE.subprocess, "Popen", side_effect=self.popen), \
             mock.patch.object(VALIDATE.os, "killpg", side_effect=self.killpg), \
             contextlib.redirect_stdout(io.StringIO()):
            return VALIDATE.run_validation(self.root, self.emulator, self.output, **kwargs)

    def test_success_records_verified_build_command_environment_and_capture_checks(self):
        result = self.run_validation()
        self.assertEqual(result["status"], "passed")
        self.assertEqual(result["scope"], "smoke_transport")
        self.assertEqual(result["configuration"], "Debug")
        self.assertEqual(result["checks"]["captures"]["valid"], 2)
        self.assertTrue(all(check["status"] == "passed" for check in result["checks"].values()))
        self.assertEqual(result["emulator_sha256"], VALIDATE.package.sha256(self.emulator))
        self.assertEqual(result["build_sha256"]["self"], VALIDATE.package.sha256(self.fixture.bin / "eboot.bin"))
        self.assertEqual(result["command"][:4], [str(self.emulator), "-g", str(self.fixture.build / "app0/Debug"), "--"])
        self.assertEqual(result["command"][4:], ["nocturne", "--smoke-dir", "/data/captures", "--smoke-seconds", "1",
                                               "--smoke-capture-at", "0,0.5", "--smoke-keys", "0.2:Escape"])
        self.assertEqual(self.processes[0].options["cwd"], self.output)
        self.assertTrue(self.processes[0].options["start_new_session"])
        self.assertEqual(result["launcher_command"][3:], result["command"])
        self.assertEqual(self.processes[0].command, result["launcher_command"])
        self.assertEqual(self.processes[0].options["env"]["HOME"], "/unchanged/home")
        self.assertEqual(result["emulator_home"], {"path": str(self.output / "user/home"),
                                                  "previous_home_dir": "", "user_ids": [1000, 1001, 1002, 1003]})
        self.assertEqual(json.loads((self.output / "user/config.json").read_text())["General"]["home_dir"],
                         str(self.output / "user/home"))
        for user_id in result["emulator_home"]["user_ids"]:
            for name in ("savedata", "trophy", "inputs"):
                self.assertTrue((self.output / "user/home" / str(user_id) / name).is_dir())
        self.assertNotIn("SDL_AUDIODRIVER", self.processes[0].options["env"])
        self.assertTrue(result["environment"]["removed_dummy_audio_driver"])
        self.assertTrue(all(value == "not_assessed" for value in result["observations"]["manual_checks"].values()))
        self.assertEqual(result["observations"]["movie_audio_queue"]["nonzero_samples"], 4)
        self.assertIn("save/new.noc", result["observations"]["game_profile_files"])
        self.assertEqual(json.loads((self.output / "result.json").read_text()), result)
        self.assertEqual((self.output / "emulator.log").read_bytes(), b"fake emulator output\n")

    def test_missing_partial_corrupt_or_unfinished_capture_is_incomplete_not_stalled(self):
        for mode in ("missing", "partial", "corrupt", "no-quit", "no-keys", "wrong-key", "extra-key"):
            with self.subTest(mode=mode):
                self.mode = mode
                self.output = self.root / mode
                result = self.run_validation()
                self.assertEqual(result["status"], "instrumentation_missing")
                self.assertEqual(result["checks"]["process_exit"]["status"], "passed")
                self.assertNotIn("stalled", json.dumps(result))
                self.assertTrue(any(item["indicator"] == "startup" for item in result["observations"]["log_indicators"]))
                if mode == "corrupt":
                    self.assertEqual(len(result["checks"]["captures"]["errors"]), 1)
                if mode in ("no-keys", "wrong-key", "extra-key"):
                    self.assertEqual(result["checks"]["captures"]["status"], "passed")
                    self.assertEqual(result["checks"]["key_events"]["status"], "incomplete")

    def test_key_evidence_preserves_counts_and_accepts_sdl_name_case(self):
        self.assertEqual(self.run_validation()["status"], "passed")
        captures = self.output / "user/data/captures"
        events_path = captures / "events.csv"
        base = "\n".join(line for line in events_path.read_text().splitlines() if ",key," not in line) + "\n"
        cases = (("", [], True),
                 ("0.1:escape,0.2:LEFT CTRL,0.2:Left Shift,0.3:escape", ["Escape", "Left Ctrl", "Left Shift", "Escape"], True),
                 ("0.1:y,0.2: ", ["Y", "Space"], True),
                 ("0.1:Escape,0.2:Escape", ["Escape"], False),
                 ("0.1:Escape", ["Escape", "Escape"], False),
                 ("0.1:NotAnSDLKey", [], False))
        for schedule, observed, passed in cases:
            with self.subTest(schedule=schedule, observed=observed):
                events_path.write_text(base + "".join(f"300,key,{name}\n" for name in observed))
                _, checks = VALIDATE.inspect_capture(captures, 2, schedule)
                self.assertEqual(checks["captures"]["status"], "passed")
                self.assertEqual(checks["key_events"]["status"], "passed" if passed else "incomplete")
                self.assertEqual(checks["key_events"]["observed"], observed)

    def test_nonzero_exit_retains_captures_and_is_process_failure(self):
        self.mode = "nonzero"
        result = self.run_validation()
        self.assertEqual((result["status"], result["returncode"]), ("process_failure", 7))
        self.assertEqual(result["checks"]["captures"]["status"], "passed")
        self.assertEqual(result["checks"]["process_exit"]["status"], "failed")

    def test_timeout_remains_failure_when_terminate_exits_zero_and_kill_is_bounded(self):
        for mode in ("timeout", "kill"):
            self.mode, self.output = mode, self.root / mode
            result = self.run_validation(startup_timeout=0)
            self.assertEqual(result["status"], "timed_out")
            self.assertTrue(result["timed_out"])
            self.assertEqual(result["checks"]["process_exit"]["status"], "failed")
            self.assertTrue(self.processes[-1].terminated)
            self.assertEqual(self.processes[-1].killed, mode == "kill")
            self.assertEqual(result["returncode"], 0 if mode == "timeout" else -9)

    def test_interruption_stops_owned_process_and_restores_signal_handlers(self):
        self.mode = "interrupted"
        handlers = {value: signal.getsignal(value) for value in (signal.SIGINT, signal.SIGTERM)}
        result = self.run_validation()
        self.assertEqual(result["status"], "interrupted")
        self.assertTrue(self.processes[0].terminated)
        self.assertEqual({value: signal.getsignal(value) for value in handlers}, handlers)

    def test_launch_failure_is_reported_with_retained_evidence(self):
        with mock.patch.object(VALIDATE.subprocess, "Popen", side_effect=OSError("cannot execute fixture")), \
             contextlib.redirect_stdout(io.StringIO()):
            result = VALIDATE.run_validation(self.root, self.emulator, self.output, data_directory=self.data)
        self.assertEqual(result["status"], "process_failure")
        self.assertIn("cannot execute fixture", result["error"])
        self.assertTrue((self.output / "result.json").is_file())

    def test_stale_or_mismatched_stage_never_launches(self):
        original_stage = VALIDATE.package.stage_app0
        for mode in ("elf", "self", "file", "extra"):
            self.output = self.root / ("mismatch-" + mode)

            def bad_stage(*args):
                stage = original_stage(*args)
                manifest_path = stage / "package-manifest.json"
                manifest = json.loads(manifest_path.read_text())
                if mode == "elf":
                    manifest["elf_sha256"] = "0" * 64
                elif mode == "self":
                    binary = stage / "eboot.bin"
                    binary.write_bytes(binary.read_bytes() + b"stale build")
                    manifest["files"]["eboot.bin"] = {"size": binary.stat().st_size,
                                                       "sha256": VALIDATE.package.sha256(binary)}
                elif mode == "file":
                    (stage / "fonts/Fallback.ttf").write_bytes(b"bad font")
                else:
                    (stage / "stale-file").write_bytes(b"unexpected")
                manifest_path.write_text(json.dumps(manifest))
                return stage

            with mock.patch.object(VALIDATE.package, "stage_app0", side_effect=bad_stage):
                result = self.run_validation()
            self.assertEqual(result["status"], "validation_failure")
        self.assertFalse(self.processes)

    def test_existing_output_and_unsupported_mouse_scenario_fail_before_staging(self):
        self.output.mkdir()
        (self.output / "previous").write_bytes(b"retain")
        with mock.patch.object(VALIDATE.package, "stage_app0", side_effect=AssertionError("unexpected staging")):
            with self.assertRaisesRegex(VALIDATE.ValidationError, "new directory"):
                self.run_validation()
            self.assertEqual((self.output / "previous").read_bytes(), b"retain")
            self.output = self.root / "unsupported"
            with self.assertRaisesRegex(VALIDATE.ValidationError, "desktop smoke runner"):
                self.run_validation(scenario="cursor")
            self.assertFalse(self.output.exists())

    def test_profiles_copy_only_selected_inputs_and_preserve_sources(self):
        emulator_profile, game_profile = self.root / "source emulator", self.root / "source game"
        for root, files in ((emulator_profile, {"config.json": b"{}", "users.json": b'{"Users":{"user":[]}}', "input_config/pad.ini": b"pad",
                                               "log/old.log": b"old log", "data/private": b"private"}),
                            (game_profile, {"system/nocturne.ini": b"settings", "save/old.noc": b"old save",
                                            "old.log": b"Nocturne: old profile error\n"})):
            for relative, contents in files.items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(contents)
        before = [self.fixture.tree_files(path) for path in (emulator_profile, game_profile)]
        result = self.run_validation(emulator_profile=emulator_profile, game_profile=game_profile,
                                     environment={"HOME": "/preserved", "SDL_AUDIODRIVER": "coreaudio", "OTHER": "retained"})
        self.assertEqual(result["status"], "passed")
        for name in ("users.json", "input_config/pad.ini"):
            self.assertEqual((self.output / "user" / name).read_bytes(), (emulator_profile / name).read_bytes())
        self.assertEqual(json.loads((self.output / "user/config.json").read_text()),
                         {"General": {"home_dir": str(self.output / "user/home")}})
        self.assertFalse((self.output / "user/log/old.log").exists())
        self.assertFalse((self.output / "user/data/private").exists())
        self.assertEqual((self.output / "user/data/NocturneRecompKnightStar/save/old.noc").read_bytes(), b"old save")
        self.assertEqual([self.fixture.tree_files(path) for path in (emulator_profile, game_profile)], before)
        self.assertEqual(self.processes[0].options["env"], {"HOME": "/preserved", "SDL_AUDIODRIVER": "coreaudio", "OTHER": "retained"})
        self.assertTrue(any(item["indicator"] == "runtime_error" for item in result["observations"]["retained_profile_log_indicators"]))
        self.assertFalse(any(item["indicator"] == "runtime_error" for item in result["observations"]["log_indicators"]))

    def test_copied_custom_home_is_isolated_without_changing_original_accounts(self):
        emulator_profile = self.root / "source emulator"
        emulator_profile.mkdir()
        original_home = self.root / "original account saves"
        (original_home / "42/savedata").mkdir(parents=True)
        (original_home / "42/savedata/keep.sav").write_bytes(b"original save")
        config = {"General": {"home_dir": str(original_home), "volume_slider": 73},
                  "Input": {"use_unified_input_config": False}, "unknown_setting": "retain"}
        (emulator_profile / "config.json").write_text(json.dumps(config))
        users = {"Users": {"user": [{"user_id": 42, "user_name": "copied player", "player_index": 1}]}}
        (emulator_profile / "users.json").write_text(json.dumps(users))
        before = [self.fixture.tree_files(path) for path in (emulator_profile, original_home)]
        result = self.run_validation(emulator_profile=emulator_profile)
        self.assertEqual(result["status"], "passed")
        copied = json.loads((self.output / "user/config.json").read_text())
        config["General"]["home_dir"] = str(self.output / "user/home")
        self.assertEqual(copied, config)
        self.assertEqual(result["emulator_home"]["previous_home_dir"], str(original_home))
        self.assertEqual(result["emulator_home"]["user_ids"], [42, 1000, 1001, 1002, 1003])
        for user_id in result["emulator_home"]["user_ids"]:
            for name in ("savedata", "trophy", "inputs"):
                self.assertTrue((self.output / "user/home" / str(user_id) / name).is_dir())
        self.assertEqual([self.fixture.tree_files(path) for path in (emulator_profile, original_home)], before)

    def test_copied_saves_keep_timestamp_order_and_writable_permissions(self):
        profile = self.root / "saved profile"
        (profile / "save").mkdir(parents=True)
        saves = {"AUTO.NOC": 1500000000123456789, "SLOT01.NOC": 1600000000987654321}
        for name, stamp in saves.items():
            source = profile / "save" / name
            source.write_bytes(name.encode())
            os.utime(source, ns=(stamp, stamp))
            source.chmod(0o444)
        before = {name: (path.read_bytes(), path.stat().st_mtime_ns, stat.S_IMODE(path.stat().st_mode))
                  for name in saves for path in [profile / "save" / name]}
        result = self.run_validation(game_profile=profile)
        self.assertEqual(result["status"], "passed")
        copied = self.output / "user/data/NocturneRecompKnightStar/save"
        for name in saves:
            self.assertEqual((copied / name).read_bytes(), before[name][0])
            self.assertEqual((copied / name).stat().st_mtime_ns, before[name][1])
            self.assertTrue((copied / name).stat().st_mode & stat.S_IWUSR)
        self.assertEqual(max(saves, key=lambda name: (copied / name).stat().st_mtime_ns), "SLOT01.NOC")
        self.assertEqual({name: (path.read_bytes(), path.stat().st_mtime_ns, stat.S_IMODE(path.stat().st_mode))
                          for name in saves for path in [profile / "save" / name]}, before)

    def test_invalid_account_id_never_launches_or_traverses_profile_paths(self):
        emulator_profile = self.root / "invalid account"
        emulator_profile.mkdir()
        for number, value in enumerate(("../../outside", -1, True, 0x80000000)):
            self.output = self.root / f"invalid-account-{number}"
            (emulator_profile / "users.json").write_text(json.dumps({"Users": {"user": [{"user_id": value}]}}))
            result = self.run_validation(emulator_profile=emulator_profile)
            self.assertEqual(result["status"], "validation_failure")
            self.assertIn("user_id", result["error"])
            self.assertFalse((self.output / "user/home").exists())
        self.assertFalse(self.processes)

    def test_cli_release_selects_matching_build(self):
        shutil.copytree(self.fixture.bin, self.fixture.build / "bin/Release")
        shutil.copytree(self.fixture.resources, self.fixture.build / "resources/Release")
        with mock.patch.object(VALIDATE, "ROOT", self.root), \
             mock.patch.object(VALIDATE.subprocess, "Popen", side_effect=self.popen), \
             contextlib.redirect_stdout(io.StringIO()):
            status = VALIDATE.main(["--emulator", str(self.emulator), "--output", str(self.output),
                                    "--data-dir", str(self.data), "--config=Release"])
        self.assertEqual(status, 0)
        self.assertEqual(json.loads((self.output / "result.json").read_text())["configuration"], "Release")


if __name__ == "__main__":
    unittest.main()
