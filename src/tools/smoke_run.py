#!/usr/bin/env python3
"""Run an isolated, bounded native integration capture; never controls other apps."""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time


def schedule(value, *, keys=False, mouse=False):
    for item in value.split(",") if value else []:
        seconds, separator, key = item.partition(":")
        moment = float(seconds)
        if not math.isfinite(moment) or not 0 <= moment <= 600:
            raise ValueError("Schedule seconds must be within 0..600")
        if keys and (not separator or not key.strip()):
            raise ValueError("Keys use seconds:SDL-key-name pairs")
        if mouse:
            coordinates = key.split(":")
            if not separator or len(coordinates) != 2:
                raise ValueError("Mouse motion uses seconds:x:y in logical framebuffer coordinates")
            try:
                x, y = map(int, coordinates)
            except ValueError:
                raise ValueError("Mouse coordinates must be integers") from None
            if not (0 <= x < 16384 and 0 <= y < 16384):
                raise ValueError("Mouse coordinates must be within 0..16383")
        if not keys and not mouse and separator:
            raise ValueError("Capture times contain seconds only")
    return value


def main():
    scenarios = json.loads((Path(__file__).resolve().parents[1] / "tests/smoke-scenarios.json").read_text())
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--data-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--user-dir", type=Path,
                        help="Reuse an explicit test profile for save/relaunch checks; otherwise isolate under output")
    parser.add_argument("--scenario", choices=sorted(scenarios))
    parser.add_argument("--seconds", type=float)
    parser.add_argument("--capture-at")
    parser.add_argument("--keys")
    parser.add_argument("--mouse-at", help="Comma-separated seconds:x:y motions in logical framebuffer coordinates; no OS pointer warp")
    args = parser.parse_args()
    defaults = scenarios.get(args.scenario, {"seconds": 30, "capture_at": "1,5,10,20", "keys": ""})
    for key in ("seconds", "capture_at", "keys"):
        if getattr(args, key) is None:
            setattr(args, key, defaults[key])
    if args.mouse_at is None:
        args.mouse_at = defaults.get("mouse_at", "")
    app = args.app.resolve()
    if app.suffix == ".app":
        app = app / "Contents/MacOS/NocturneRecompKnightStar"
    if not app.is_file() or not args.data_dir.is_dir():
        parser.error("App executable and data directory must exist")
    if not math.isfinite(args.seconds) or not 1 <= args.seconds <= 600:
        parser.error("--seconds must be within 1..600")
    try:
        schedule(args.capture_at)
        schedule(args.keys, keys=True)
        schedule(args.mouse_at, mouse=True)
    except ValueError as error:
        parser.error(str(error))
    output = args.output.resolve()
    if output.exists():
        parser.error("--output must be a new directory to preserve previous evidence")
    output.mkdir(parents=True)
    env = os.environ.copy()
    env.update(NOCTURNE_SMOKE_DIR=str(output), NOCTURNE_SMOKE_SECONDS=str(args.seconds),
               NOCTURNE_SMOKE_CAPTURE_AT=args.capture_at, NOCTURNE_SMOKE_KEYS=args.keys,
               NOCTURNE_SMOKE_MOUSE_AT=args.mouse_at)
    command = [str(app), "--data-dir", str(args.data_dir.resolve()),
               "--user-dir", str(args.user_dir.resolve() if args.user_dir else output / "user")]
    began = time.monotonic()
    timed_out = False
    with (output / "console.log").open("wb") as log:
        process = subprocess.Popen(command, cwd="/", env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            result = process.wait(timeout=args.seconds + 20)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.terminate()
            try:
                result = process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                result = process.wait()
    frames = []
    for file in sorted(output.glob("frame-*.ppm")):
        data = file.read_bytes()
        frames.append({"file": file.name, "bytes": len(data),
                       "sha256": hashlib.sha256(data).hexdigest()})
    queued_samples = nonzero_samples = 0
    audio = output / "movie-audio.csv"
    if audio.exists():
        with audio.open() as file:
            for row in csv.DictReader(file):
                queued_samples += int(row["samples"])
                nonzero_samples += int(row["nonzero"])
    native_cursor = []
    mouse_events = []
    events = output / "events.csv"
    if events.exists():
        with events.open() as file:
            for row in csv.DictReader(file):
                if row["event"] == "native_cursor":
                    native_cursor.append({"elapsed_ms": int(row["elapsed_ms"]),
                                          "state": int(row["detail"])})
                elif row["event"] == "mouse":
                    mouse_events.append({"elapsed_ms": int(row["elapsed_ms"]), "detail": row["detail"]})
    report = {"command": command, "returncode": result, "timed_out": timed_out,
              "elapsed_seconds": round(time.monotonic() - began, 2), "frames": frames,
              "movie_samples_queued": queued_samples, "nonzero_movie_samples_queued": nonzero_samples,
              "keys": args.keys,
              "mouse_at": args.mouse_at, "mouse_events": mouse_events, "native_cursor": native_cursor,
              "limits": "Captures require visual inspection. PCM queued to SDL does not verify audible output. "
                        "Native cursor state is SDL's visibility query (0 hidden; 1 shown); the GL image excludes it. "
                        "A bounded run does not establish campaign or physics correctness."}
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if result == 0 and not timed_out and frames else 1


if __name__ == "__main__":
    sys.exit(main())
