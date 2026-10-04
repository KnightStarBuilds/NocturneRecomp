#!/usr/bin/env python3
"""Run a bounded shadPS4 smoke capture with isolated profiles and retained evidence."""
import argparse
import csv
import importlib.util
import json
import math
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("nocturne_ps4_package", ROOT / "ps4/package.py")
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


class ValidationError(Exception):
    pass


class InterruptedRun(Exception):
    def __init__(self, signum):
        self.signum = signum


def write_result(output, result):
    temporary = output / "result.json.tmp"
    temporary.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, output / "result.json")


def profile_inputs(source, *, emulator=False):
    if source is None:
        return []
    if source.is_symlink() or not source.is_dir():
        raise ValidationError(f"Profile must be a regular directory: {source}")
    if not emulator:
        return package.inventory(source, "")
    inputs = []
    for name in ("config.json", "users.json", "input_config"):
        path = source / name
        if not path.exists() and not path.is_symlink():
            continue
        if name == "input_config":
            inputs += package.inventory(path, name)
        else:
            package.regular_file(path)
            inputs.append((path, name))
    return inputs


def copy_inputs(inputs, destination):
    destination.mkdir(parents=True, exist_ok=True)
    for source, relative in inputs:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        timestamps = source.stat()
        shutil.copyfile(source, target)
        # Continue chooses the newest save by mtime. Retain that ordering while
        # keeping the copied profile writable even when its source is read-only.
        os.utime(target, ns=(timestamps.st_atime_ns, timestamps.st_mtime_ns))


def prepare_emulator_home(user_directory):
    """Keep emulator account saves inside the copied portable profile."""
    if user_directory.is_symlink():
        raise ValidationError("Portable emulator profile must not be a symlink")
    user_directory = user_directory.resolve()
    config_path = user_directory / "config.json"
    if config_path.exists() or config_path.is_symlink():
        package.regular_file(config_path)
    config = json.loads(config_path.read_text()) if config_path.exists() else {}
    if not isinstance(config, dict) or not isinstance(config.get("General", {}), dict):
        raise ValidationError("Emulator config.json must contain an object with General settings")
    general = config.setdefault("General", {})
    previous = general.get("home_dir", "")
    # This revision constructs default users even while loading users.json.
    # Missing home/1000 triggers a modal before checking for any old saves.
    user_ids = {1000, 1001, 1002, 1003}
    users_path = user_directory / "users.json"
    if users_path.exists() or users_path.is_symlink():
        package.regular_file(users_path)
        users = json.loads(users_path.read_text())
        if not isinstance(users, dict) or not isinstance(users.get("Users", {}), dict):
            raise ValidationError("Emulator users.json must contain a Users object")
        accounts = users.get("Users", {}).get("user", [])
        if not isinstance(accounts, list):
            raise ValidationError("Emulator Users.user must be a list")
        for account in accounts:
            user_id = account.get("user_id") if isinstance(account, dict) else None
            if type(user_id) is not int or not 0 <= user_id <= 0x7fffffff:
                raise ValidationError("Emulator user_id must be a nonnegative signed 32-bit integer")
            user_ids.add(user_id)
    home = user_directory / "home"
    directories = [home]
    for user_id in sorted(user_ids):
        account = home / str(user_id)
        directories += [account, *(account / name for name in ("savedata", "trophy", "inputs"))]
    for directory in directories:
        if directory.is_symlink():
            raise ValidationError(f"Isolated emulator home must not contain symlinks: {directory}")
        directory.mkdir(exist_ok=True)
    general["home_dir"] = str(home)
    config_path.write_text(json.dumps(config, indent=2) + "\n")
    return {"path": str(home), "previous_home_dir": previous, "user_ids": sorted(user_ids)}


def verify_stage(root, config, stage):
    manifest_path = stage / "package-manifest.json"
    package.regular_file(manifest_path)
    manifest = json.loads(manifest_path.read_text())
    build = root / "build/ps4-x86_64/bin" / config
    elf_hash = package.sha256(build / "nocturne.elf")
    self_hash = package.sha256(build / "eboot.bin")
    if (not isinstance(manifest, dict) or manifest.get("format") != 1 or manifest.get("configuration") != config or
            manifest.get("elf_sha256") != elf_hash):
        raise ValidationError("Staged manifest does not match the selected configuration and ELF")
    files = manifest.get("files")
    if not isinstance(files, dict) or "eboot.bin" not in files:
        raise ValidationError("Staged manifest has no executable inventory")
    actual = {relative for _, relative in package.inventory(stage, "")}
    if actual != set(files) | {"package-manifest.json"}:
        raise ValidationError("Staged file inventory differs from its manifest")
    for relative, expected in files.items():
        path = Path(relative)
        if path.is_absolute() or ".." in path.parts or not isinstance(expected, dict):
            raise ValidationError(f"Invalid manifest entry: {relative}")
        file = stage / relative
        if file.stat().st_size != expected.get("size") or package.sha256(file) != expected.get("sha256"):
            raise ValidationError(f"Staged file does not match its manifest: {relative}")
    if files["eboot.bin"]["sha256"] != self_hash:
        raise ValidationError("Staged SELF does not match the selected build")
    return {"elf": elf_hash, "self": self_hash, "manifest": package.sha256(manifest_path)}


def ppm_details(path):
    """Check the binary P6 format emitted by smoke_probe without decoding images."""
    package.regular_file(path)
    with path.open("rb") as stream:
        header = stream.read(4096)
    offset, tokens = 0, []
    while len(tokens) < 4:
        while offset < len(header) and header[offset] in b" \t\r\n":
            offset += 1
        if offset < len(header) and header[offset] == ord("#"):
            end = header.find(b"\n", offset)
            if end < 0:
                raise ValueError("unterminated PPM comment")
            offset = end + 1
            continue
        end = offset
        while end < len(header) and header[end] not in b" \t\r\n":
            end += 1
        if end == offset or end == len(header):
            raise ValueError("incomplete PPM header")
        tokens.append(header[offset:end])
        offset = end
    width, height, maximum = map(int, tokens[1:])
    if tokens[0] != b"P6" or maximum != 255 or not (1 <= width <= 16384 and 1 <= height <= 16384):
        raise ValueError("expected nonempty P6 RGB pixels with maximum 255")
    offset += 2 if header[offset:offset + 2] == b"\r\n" else 1
    if path.stat().st_size != offset + width * height * 3:
        raise ValueError("PPM raster length does not match its dimensions")
    return {"file": path.name, "width": width, "height": height,
            "bytes": path.stat().st_size, "sha256": package.sha256(path)}


def inspect_capture(captures, expected_count, key_schedule):
    frames, errors = [], []
    for path in sorted(captures.glob("frame-*.ppm")):
        try:
            frames.append(ppm_details(path))
        except (OSError, ValueError, package.PackageError) as error:
            errors.append({"file": path.name, "error": str(error)})
    rows = []
    events_error = None
    try:
        with (captures / "events.csv").open(newline="") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != ["elapsed_ms", "event", "detail"]:
                raise ValueError("unexpected events.csv columns")
            rows = list(reader)
            if any(row.get("detail") is None or not row.get("elapsed_ms", "").isdigit() for row in rows):
                raise ValueError("invalid event record")
    except (OSError, ValueError, csv.Error) as error:
        events_error = str(error)
    captured = [row["detail"] for row in rows if row.get("event") == "capture"]
    capture_ok = (not errors and len(frames) == expected_count and expected_count > 0 and
                  sorted(captured) == sorted(frame["file"] for frame in frames))
    expected_keys = [item.partition(":")[2] for item in key_schedule.split(",")] if key_schedule else []
    observed_keys = [row["detail"] for row in rows if row.get("event") == "key"]
    # SDL canonicalizes letter-key case and names the literal space "Space".
    # Retain duplicates: two scheduled presses require two logged attempts.
    normalize_key = lambda name: "space" if name == " " else name.casefold()
    key_events_ok = (not events_error and
                     sorted(map(normalize_key, expected_keys)) == sorted(map(normalize_key, observed_keys)))
    quit_requested = not events_error and any(row["event"] == "quit" and row["detail"] == "requested" for row in rows)
    return frames, {"captures": {"status": "passed" if capture_ok else "incomplete",
                                  "expected": expected_count, "valid": len(frames), "errors": errors},
                    "key_events": {"status": "passed" if key_events_ok else "incomplete",
                                   "expected": expected_keys, "observed": observed_keys},
                    "quit_requested": {"status": "passed" if quit_requested else "incomplete",
                                       "events_error": events_error}}


def observe_logs(output, scenario, copied_logs):
    indicators = {"startup": "beginning runtime preparation", "runtime_error": "Nocturne:", "path_error": "Cannot resolve runtime directory",
                  "cwd_error": "Cannot select user directory", "segfault": "SIGSEGV", "sigsys": "SIGSYS"}
    if scenario.get("mission"):
        indicators["scenario_mission_mentioned"] = scenario["mission"]
    indicators.update(movie_open="mci open", movie_drained="mci drained")
    matches, retained = [], []
    paths = [output / "emulator.log", *(output / "user/data/NocturneRecompKnightStar").glob("*.log"),
             output / "user/data/NocturneRecompKnightStar/stderr.txt"]
    for directory in (output / "user/log", output / "user/logs"):
        if directory.is_dir() and not directory.is_symlink():
            paths += [path for path in directory.rglob("*") if path.suffix in (".log", ".txt")]
    for path in paths:
        if not path.is_file() or path.is_symlink():
            continue
        relative = str(path.relative_to(output))
        copied = copied_logs.get(relative)
        unchanged = copied and path.stat().st_mtime_ns == copied["mtime_ns"] and package.sha256(path) == copied["sha256"]
        found = set()
        with path.open(errors="replace") as stream:
            for line in stream:
                for name, needle in indicators.items():
                    if name not in found and needle.casefold() in line.casefold():
                        found.add(name)
                        (retained if unchanged else matches).append({"file": relative, "indicator": name, "line": line.strip()[:400]})
    profile = output / "user/data/NocturneRecompKnightStar"
    files = {relative: {"size": path.stat().st_size, "sha256": package.sha256(path)}
             for path, relative in package.inventory(profile, "")}
    audio = {"status": "not_observed", "queued_samples": 0, "nonzero_samples": 0}
    audio_path = output / "user/data/captures/movie-audio.csv"
    if audio_path.is_file() and not audio_path.is_symlink():
        try:
            with audio_path.open(newline="") as stream:
                for row in csv.DictReader(stream):
                    samples, nonzero = int(row["samples"]), int(row["nonzero"])
                    if not 0 <= nonzero <= samples:
                        raise ValueError("invalid audio sample counts")
                    audio["queued_samples"] += samples
                    audio["nonzero_samples"] += nonzero
            audio["status"] = "observed" if audio["queued_samples"] else "no_samples"
        except (KeyError, ValueError, csv.Error) as error:
            audio.update(status="invalid", error=str(error))
    return {"log_indicators": matches, "retained_profile_log_indicators": retained, "game_profile_files": files,
            "movie_audio_queue": audio,
            "manual_checks": {name: "not_assessed" for name in ("rendered_content", "controller", "audible_audio", "save_load")}}


def stop_owned_process(process):
    if process.poll() is not None:
        return process.returncode
    # Popen creates a new session, so this group contains only our launcher,
    # emulator and its descendants, including an exec blocked under Rosetta.
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        return process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        return process.wait(timeout=5)


def run_validation(root, emulator, output, *, config="Debug", scenario="menu", data_directory=None,
                   emulator_profile=None, game_profile=None, startup_timeout=120, environment=None):
    root = root.resolve()
    if sys.platform not in ("darwin", "linux"):
        raise ValidationError("The emulator validator currently supports macOS and Linux hosts")
    emulator = emulator.expanduser().resolve()
    output = output.expanduser().absolute()
    if output.exists() or output.is_symlink():
        raise ValidationError("--output must be a new directory to preserve previous evidence")
    if not emulator.is_file() or not os.access(emulator, os.X_OK):
        raise ValidationError("--emulator must name an executable file")
    if config not in ("Debug", "Release") or not math.isfinite(startup_timeout) or startup_timeout < 0:
        raise ValidationError("Invalid configuration or startup timeout")
    scenarios = json.loads((root / "src/tests/smoke-scenarios.json").read_text())
    if scenario not in scenarios:
        raise ValidationError(f"Unknown smoke scenario: {scenario}")
    settings = scenarios[scenario]
    if settings.get("mouse_at"):
        raise ValidationError("Scenarios with mouse_at require the desktop smoke runner")
    seconds = settings["seconds"]
    if not math.isfinite(seconds) or not 1 <= seconds <= 600:
        raise ValidationError("Scenario seconds must be within 1..600")
    data_directory = (data_directory or root / "data/original").expanduser().resolve()
    emulator_profile = emulator_profile.expanduser().absolute() if emulator_profile else None
    game_profile = game_profile.expanduser().absolute() if game_profile else None
    emulator_inputs = profile_inputs(emulator_profile, emulator=True)
    game_inputs = profile_inputs(game_profile)
    for protected in (data_directory, emulator_profile, game_profile, root / "build/ps4-x86_64/app0"):
        if protected and (output.resolve().is_relative_to(protected.resolve()) or protected.resolve().is_relative_to(output.resolve())):
            raise ValidationError(f"Output must not overlap assets, source profiles or app0: {protected}")
    output.mkdir(parents=True)
    output = output.resolve()
    environment = dict(os.environ if environment is None else environment)
    removed_dummy = environment.get("SDL_AUDIODRIVER") == "dummy"
    if removed_dummy:
        environment.pop("SDL_AUDIODRIVER")
    result = {"scope": "smoke_transport", "status": "preparing", "configuration": config,
              "scenario": scenario, "scenario_settings": settings, "output": str(output),
              "started_utc": datetime.now(timezone.utc).isoformat(), "pid": None, "command": None, "launcher_command": None,
              "emulator": str(emulator), "emulator_sha256": package.sha256(emulator),
              "emulator_profile_source": str(emulator_profile) if emulator_profile else None,
              "game_profile_source": str(game_profile) if game_profile else None,
              "emulator_profile": str(output / "user"), "game_profile": str(output / "user/data/NocturneRecompKnightStar"),
              "environment": {"SDL_AUDIODRIVER": environment.get("SDL_AUDIODRIVER"),
                              "SDL_VIDEODRIVER": environment.get("SDL_VIDEODRIVER"),
                              "removed_dummy_audio_driver": removed_dummy},
              "deadline_seconds": seconds + startup_timeout, "returncode": None, "timed_out": False,
              "checks": {}, "copied_game_logs": {},
              "observations": {"manual_checks": {name: "not_assessed" for name in ("rendered_content", "controller", "audible_audio", "save_load")}},
              "limits": "Passed means smoke transport: validated captures, logged scheduled key attempts, requested quit and clean exit. "
              "Key logs do not prove UI response. Frames require visual inspection; queued PCM does not prove audible sound. "
              "Campaign and physics are not certified."}
    began = time.monotonic()
    write_result(output, result)
    process = None
    previous_handlers = {}
    captures = output / "user/data/captures"
    try:
        copy_inputs(emulator_inputs, output / "user")
        result["emulator_home"] = prepare_emulator_home(output / "user")
        copy_inputs(game_inputs, output / "user/data/NocturneRecompKnightStar")
        for _, relative in game_inputs:
            if Path(relative).suffix in (".log", ".txt"):
                path = output / "user/data/NocturneRecompKnightStar" / relative
                result["copied_game_logs"][str(path.relative_to(output))] = {
                    "mtime_ns": path.stat().st_mtime_ns, "sha256": package.sha256(path)}
        captures.mkdir(parents=True)
        stage = package.stage_app0(root, config, data_directory)
        result["app0"] = str(stage)
        result["build_sha256"] = verify_stage(root, config, stage)
        result["checks"]["staged_build"] = {"status": "passed"}
        shutil.copyfile(stage / "package-manifest.json", output / "app0-manifest.json")
        # shadPS4 forwards all post-'--' arguments without inserting argv[0].
        command = [str(emulator), "-g", str(stage), "--", "nocturne", "--smoke-dir", "/data/captures",
                   "--smoke-seconds", str(seconds), "--smoke-capture-at", settings["capture_at"],
                   "--smoke-keys", settings["keys"]]
        result["command"] = command
        # Complete a native Python exec first; its same-PID exec of shadPS4 is
        # then covered by wait's deadline even when Rosetta launch blocks.
        launcher = [sys.executable, "-c", "import os,sys; os.execv(sys.argv[1],sys.argv[1:])", *command]
        result["launcher_command"] = launcher

        def interrupted(signum, _frame):
            raise InterruptedRun(signum)

        for signum in (signal.SIGINT, signal.SIGTERM):
            previous_handlers[signum] = signal.signal(signum, interrupted)
        with (output / "emulator.log").open("wb") as log:
            process = subprocess.Popen(launcher, cwd=output, env=environment, stdout=log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            result.update(status="running", pid=process.pid)
            write_result(output, result)
            try:
                result["returncode"] = process.wait(timeout=seconds + startup_timeout)
            except subprocess.TimeoutExpired:
                result["timed_out"] = True
                result["returncode"] = stop_owned_process(process)
    except InterruptedRun as error:
        result["interrupted_signal"] = error.signum
    except (OSError, ValueError, ValidationError, package.PackageError, subprocess.TimeoutExpired) as error:
        result["error"] = str(error)
        result["status"] = "process_failure" if result["command"] else "validation_failure"
    finally:
        # Finish cleanup even if another Ctrl-C arrives, then restore callers'
        # handlers. Only this runner's Popen instance is ever signalled.
        for signum in previous_handlers:
            signal.signal(signum, signal.SIG_IGN)
        if process and process.poll() is None:
            try:
                result["returncode"] = stop_owned_process(process)
            except (OSError, subprocess.TimeoutExpired) as error:
                result["cleanup_error"] = str(error)
        elif process and result["returncode"] is None:
            result["returncode"] = process.returncode
        for signum, handler in previous_handlers.items():
            signal.signal(signum, handler)
        expected_count = len(settings["capture_at"].split(",")) if settings["capture_at"] else 0
        result["frames"], checks = inspect_capture(captures, expected_count, settings["keys"])
        result["checks"].update(checks)
        clean_exit = result["returncode"] == 0 and not result["timed_out"] and not result.get("interrupted_signal")
        result["checks"]["process_exit"] = {"status": "passed" if clean_exit else "failed"}
        if result.get("interrupted_signal"):
            result["status"] = "interrupted"
        elif result["timed_out"]:
            result["status"] = "timed_out"
        elif "error" not in result:
            result["status"] = ("process_failure" if not clean_exit else "passed" if
                                all(check["status"] == "passed" for check in checks.values()) else "instrumentation_missing")
        try:
            result["observations"] = observe_logs(output, settings, result["copied_game_logs"])
        except (OSError, package.PackageError) as error:
            result["observation_error"] = str(error)
        result["elapsed_seconds"] = round(time.monotonic() - began, 3)
        write_result(output, result)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--emulator", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--config", choices=("Debug", "Release"), default="Debug")
    parser.add_argument("--scenario", default="menu")
    parser.add_argument("--emulator-profile", type=Path)
    parser.add_argument("--game-profile", type=Path)
    parser.add_argument("--data-dir", type=Path)
    parser.add_argument("--startup-timeout", type=float, default=120, help="extra startup allowance in seconds (default: 120)")
    args = parser.parse_args(argv)
    try:
        result = run_validation(ROOT, args.emulator, args.output, config=args.config, scenario=args.scenario,
                                data_directory=args.data_dir, emulator_profile=args.emulator_profile,
                                game_profile=args.game_profile, startup_timeout=args.startup_timeout)
    except (OSError, ValueError, ValidationError, package.PackageError) as error:
        parser.error(str(error))
    print(f"{result['status']}: {args.output.resolve() / 'result.json'}")
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
