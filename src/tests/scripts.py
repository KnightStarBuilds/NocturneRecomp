#!/usr/bin/env python3
"""Test real launch/preparation scripts in an isolated project with fake build tools.

Only compiler/CMake/host discovery are mocked. Archive checksums, extraction,
incremental stamps and clean behavior use the actual preparer. Python audit
hooks reject access to retired patch/source inputs or copying maintained code.
The real project, dependency cache, game data and current build are never mutated.
"""
from pathlib import Path
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def snapshot(directory):
    return {path.relative_to(directory).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in directory.rglob("*") if path.is_file()}


def executable(path, source):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("#!" + sys.executable + "\n" + source)
    path.chmod(0o755)


def archive(path, name, content):
    with tarfile.open(path, "w:gz") as output:
        item = tarfile.TarInfo(name + "/source.txt")
        item.size = len(content)
        output.addfile(item, io.BytesIO(content))
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    with tempfile.TemporaryDirectory(prefix="nocturne script checks ") as temporary:
        project = Path(temporary).resolve() / "project with spaces"
        project.mkdir()
        for script in ("build.sh", "run.sh"):
            shutil.copy2(ROOT / script, project / script)
        (project / "deps").mkdir()
        shutil.copy2(ROOT / "deps/prepare.py", project / "deps/prepare.py")
        cache = project / "deps/cache"
        cache.mkdir()
        (project / "data/original").mkdir(parents=True)
        (project / "data/original/ACT1.POD").write_bytes(b"immutable game fixture")
        source = project / "src/game/core/game.cpp/fixture.keep.cpp"
        source.parent.mkdir(parents=True)
        source.write_text("// Address: 004dc3e0\nint fixture() { return 1; }\n")
        (project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.24)\nproject(fixture LANGUAGES CXX)\n'
            'add_executable(fixture src/game/core/game.cpp/fixture.keep.cpp)\n')
        entries = []
        for name in ("sdl2", "sdl2_ttf", "ffmpeg", "liberation-fonts"):
            digest = archive(cache / (name + ".tar.gz"), name, b"original source\n")
            entries.append({"name": name, "version": "test", "archive": name + ".tar.gz",
                            "sha256": digest, "url": "https://invalid.example/no-network"})
        (project / "deps/manifest.json").write_text(json.dumps({"dependencies": entries}))
        # Retired inputs deliberately remain present. Preparation must not
        # inspect them, even if an old checkout still has its cache or patches.
        (project / "deps/patches").mkdir()
        (project / "deps/patches/010-retired.patch").write_text("not a valid patch\n")
        (cache / "nocturne").mkdir()
        (cache / "nocturne/source.txt").write_text("not maintained source\n")
        audit = project / "audit guard"
        audit.mkdir()
        (audit / "sitecustomize.py").write_text(
            "import os, sys\n"
            "root = os.environ['NOCTURNE_TEST_PROJECT']\n"
            "retired = tuple(root + suffix for suffix in ['/deps/patches', '/deps/cache/nocturne'])\n"
            "def guard(event, args):\n"
            "    if event in ('open', 'os.listdir', 'os.scandir'):\n"
            "        path = args[0]\n"
            "        if isinstance(path, (str, bytes)):\n"
            "            path = os.path.abspath(os.fsdecode(path))\n"
            "            if any(path == base or path.startswith(base + '/') for base in retired):\n"
            "                raise RuntimeError('Build accessed retired patch/source input: ' + path)\n"
            "    if event in ('shutil.copytree', 'shutil.copyfile'):\n"
            "        source = os.path.abspath(os.fsdecode(args[0]))\n"
            "        if source == root + '/src' or source.startswith(root + '/src/'):\n"
            "            raise RuntimeError('Build copied maintained source: ' + source)\n"
            "sys.addaudithook(guard)\n")
        tools = project / "fake tools"
        calls = project / "tool-calls.jsonl"
        log = ("import json, os, sys\n"
               "from pathlib import Path\n"
               "with open(os.environ['NOCTURNE_TEST_CALLS'], 'a') as stream:\n"
               "    stream.write(json.dumps({'tool': Path(sys.argv[0]).name, 'args': sys.argv[1:], "
               "'compiler_env': {key: os.environ.get(key) for key in ['CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS']}}) + '\\n')\n")
        executable(tools / "uname", "import os, sys\n"
                   "print(os.environ.get('NOCTURNE_TEST_HOST', 'Darwin') if sys.argv[1] == '-s' "
                   "else os.environ.get('NOCTURNE_TEST_ARCH', 'arm64'))\n")
        executable(tools / "xcrun", log + "print('/fake SDK' if sys.argv[1] == '--show-sdk-path' else '/fake compiler/' + sys.argv[2])\n")
        executable(tools / "cmake", log)
        executable(tools / "ninja", log)
        executable(tools / "curl", log + "sys.exit('Network access attempted during an offline test')\n")
        executable(tools / "patch", log + "sys.exit('Build attempted to apply a retired source patch')\n")
        (tools / "python3").symlink_to(sys.executable)
        environ = os.environ.copy()
        environ.update({"PATH": str(tools) + os.pathsep + environ.get("PATH", ""),
                        "NOCTURNE_TEST_CALLS": str(calls), "NOCTURNE_OFFLINE": "1",
                        "NOCTURNE_TEST_PROJECT": str(project), "PYTHONPATH": str(audit),
                        "CMAKE_BUILD_PARALLEL_LEVEL": "2",
                        "CC": "inherited compiler", "CXX": "inherited compiler",
                        "CFLAGS": "inherited flags", "CXXFLAGS": "inherited flags",
                        "CPPFLAGS": "inherited flags", "LDFLAGS": "inherited flags"})
        # Confirm the subprocess guard is active before relying on its silence.
        for statement, diagnostic in (
            ("import os; os.listdir('deps/patches')", "Build accessed retired patch/source input"),
            ("import shutil; shutil.copyfile('src/game/core/game.cpp/fixture.keep.cpp', 'copied.cpp')",
             "Build copied maintained source"),
        ):
            result = subprocess.run([sys.executable, "-c", statement], cwd=project, env=environ,
                                    capture_output=True, text=True)
            check(result.returncode != 0 and diagnostic in result.stderr,
                  "subprocess source-ownership audit guard was not active")
        build = project / "build/macos-arm64"
        build.mkdir(parents=True)
        sentinel = build / "must survive invalid clean"
        sentinel.write_text("untouched")

        def run(script, *arguments, expected=0):
            result = subprocess.run([str(project / script), *arguments], cwd="/", env=environ,
                                    capture_output=True, text=True)
            check(result.returncode == expected,
                  f"{script} {arguments}: exit {result.returncode}, expected {expected}\n{result.stdout}\n{result.stderr}")
            return result

        def log_calls():
            return [json.loads(line) for line in calls.read_text().splitlines()] if calls.exists() else []

        protected = snapshot(project)
        invalid_build = [("--clean", "--unknown"), ("--clean", "--config=Invalid"),
                         ("--clean", "--config", "Release"), ("--config="),
                         ("--help", "--clean", "--unknown")]
        for arguments in invalid_build:
            if isinstance(arguments, str):
                arguments = (arguments,)
            run("build.sh", *arguments, expected=2)
            check(snapshot(project) == protected, "invalid build arguments mutated project state")
        for arguments in [("--unknown",), ("--config=Invalid",), ("--help", "--unknown")]:
            run("run.sh", *arguments, expected=2)
            check(snapshot(project) == protected, "invalid launcher arguments mutated project state")
        run("build.sh", "--help")
        check(snapshot(project) == protected, "build help invoked preparation")

        run("build.sh")
        cmake_calls = [call for call in log_calls() if call["tool"] == "cmake"]
        check(cmake_calls[-1]["args"] == ["--build", str(build), "--config", "Debug", "--parallel", "2"],
              "default build must select incremental Debug through --config")
        check(all(value is None for value in cmake_calls[0]["compiler_env"].values()),
              "Apple build inherited external compiler flags")
        check("Ninja Multi-Config" in cmake_calls[0]["args"] and "-DCMAKE_CXX_COMPILER=/fake compiler/clang++" in cmake_calls[0]["args"],
              "configure lost generator or xcrun compiler path quoting")
        check(not (build / "upstream").exists(), "build recreated a generated game source tree")
        check(cmake_calls[0]["args"][:2] == ["-S", str(project)],
              "configure did not use the maintained project source")
        check((build / "dependency-sources/sdl2/source.txt").read_text() == "original source\n",
              "external dependency build copy was not prepared")
        check(sentinel.exists(), "incremental build removed existing output")
        cache_before = snapshot(cache)
        data_before = snapshot(project / "data")
        # A normal source edit must survive both incremental preparation and
        # --clean without being replaced by dependency preparation.
        source.write_text("// Address: 004dc3e0\nint fixture() { return 2; }\n")
        source_before = snapshot(project / "src")
        stamp_time = (build / ".sdl2-prepared").stat().st_mtime_ns
        run("build.sh", "--config=Release")
        check((build / ".sdl2-prepared").stat().st_mtime_ns == stamp_time,
              "unchanged offline rebuild regenerated prepared source")
        check(snapshot(project / "src") == source_before,
              "incremental build replaced a direct game source edit")
        check([call for call in log_calls() if call["tool"] == "cmake"][-1]["args"][3] == "Release", "Release configuration not passed to build")
        run("build.sh", "--clean")
        check(not sentinel.exists(), "clean left stale build outputs")
        check(snapshot(cache) == cache_before and snapshot(project / "data") == data_before,
              "clean changed dependency cache or game data")
        check(snapshot(project / "src") == source_before, "clean replaced a direct game source edit")
        check(not (build / "upstream").exists(), "clean recreated a generated game source tree")
        check((build / "dependency-sources/sdl2/source.txt").read_text() == "original source\n",
              "clean did not prepare external dependencies offline")
        check(not any(call["tool"] in ("curl", "patch") for call in log_calls()),
              "offline build downloaded inputs or invoked patch")

        for config in ("Debug", "Release"):
            binary = build / f"bin/{config}/NocturneRecompKnightStar.app/Contents/MacOS/NocturneRecompKnightStar"
            executable(binary, "import json, sys\nprint(json.dumps(sys.argv[1:]))\n")
        arguments = ["--user-dir", str(project / "user saves"), "literal $HOME `characters`", "$(touch unexpected)"]
        result = run("run.sh", "--config=Release", "--", *arguments)
        check(json.loads(result.stdout) == ["--data-dir", str(project / "data/original"), *arguments],
              "launcher split or evaluated literal argument data")
        check(json.loads(run("run.sh").stdout) == ["--data-dir", str(project / "data/original")],
              "launcher default is not Debug")
        check(json.loads(run("build.sh", "--run", "--config=Release").stdout.splitlines()[-1]) ==
              ["--data-dir", str(project / "data/original")], "build --run did not invoke selected configuration")

        # Exercise the retained host branches without claiming a native build
        # on those platforms: only uname and the executable are fixtures.
        for host, platform, suffix in (("Linux", "linux", ""), ("MINGW64_NT-10.0", "windows", ".exe")):
            environ.update(NOCTURNE_TEST_HOST=host, NOCTURNE_TEST_ARCH="x86_64")
            for config in ("Debug", "Release"):
                binary = project / f"build/{platform}-x86_64/bin/{config}/nocturne_recomp_knightStar{suffix}"
                executable(binary, "import json, sys\n"
                           "print(json.dumps({'executable': sys.argv[0], 'args': sys.argv[1:]}))\n")
                selection = [] if config == "Debug" else ["--config=Release"]
                result = json.loads(run("run.sh", *selection, "--", *arguments).stdout)
                check(result == {"executable": str(binary),
                                 "args": ["--data-dir", str(project / "data/original"), *arguments]},
                      f"{platform} launcher lost output name, configuration or argument quoting")
        environ.pop("NOCTURNE_TEST_HOST")
        environ.pop("NOCTURNE_TEST_ARCH")

        fixture_archive = cache / "sdl2_ttf.tar.gz"
        # A verified archive must also identify its extracted source cache.
        # Updating a dependency pin cannot keep using an old tree just because
        # that directory already exists.
        entries[1]["sha256"] = archive(fixture_archive, "sdl2_ttf", b"updated source\n")
        entries[1]["version"] = "test-updated"
        (project / "deps/manifest.json").write_text(json.dumps({"dependencies": entries}))
        result = subprocess.run([sys.executable, str(project / "deps/prepare.py"), "--build-dir", str(build), "--offline"],
                                cwd="/", env=environ, capture_output=True, text=True)
        check(result.returncode == 0, "changed dependency pin failed preparation: " + result.stderr)
        check((cache / "sdl2_ttf/source.txt").read_bytes() == b"updated source\n", "changed pin reused stale extracted cache")
        check((build / "dependency-sources/sdl2_ttf/source.txt").read_bytes() == b"updated source\n", "changed pin reused stale build source")
        (cache / "sdl2_ttf/source.txt").write_bytes(b"untracked source edit\n")
        edited_cache = snapshot(cache)
        result = subprocess.run([sys.executable, str(project / "deps/prepare.py"), "--build-dir", str(build), "--offline"],
                                cwd="/", env=environ, capture_output=True, text=True)
        check(result.returncode != 0 and "Immutable source cache changed" in result.stderr,
              "preparation accepted a modified extracted source tree")
        check(snapshot(cache) == edited_cache, "source integrity failure changed cache")
        (cache / "sdl2_ttf/source.txt").write_bytes(b"updated source\n")
        absent_build = project / "build/verification-only"
        result = subprocess.run([sys.executable, str(project / "deps/prepare.py"), "--build-dir", str(absent_build),
                                 "--offline", "--verify-cache-only"],
                                cwd="/", env=environ, capture_output=True, text=True)
        check(result.returncode == 0 and not absent_build.exists(), "cache-only verification generated build outputs")

        # Check archive failure paths without touching real dependency archives.
        fixture_archive.write_bytes(fixture_archive.read_bytes() + b"tampered")
        tampered_cache = snapshot(cache)
        result = subprocess.run([sys.executable, str(project / "deps/prepare.py"), "--build-dir", str(build), "--offline"],
                                cwd="/", env=environ, capture_output=True, text=True)
        check(result.returncode != 0 and "SHA256 mismatch" in result.stderr, "offline preparation accepted corrupt archive")
        check(snapshot(cache) == tampered_cache, "checksum failure changed cache")
        fixture_archive.unlink()
        missing_cache = snapshot(cache)
        result = subprocess.run([sys.executable, str(project / "deps/prepare.py"), "--build-dir", str(build), "--offline"],
                                cwd="/", env=environ, capture_output=True, text=True)
        check(result.returncode != 0 and "Offline: missing" in result.stderr, "offline preparation did not reject missing archive")
        check(snapshot(cache) == missing_cache and not any(call["tool"] == "curl" for call in log_calls()),
              "offline failure mutated cache or attempted download")
        check(snapshot(project / "data") == data_before, "script checks changed game fixture")
        check(snapshot(project / "src") == source_before, "script checks changed maintained source")
    print("Script checks passed: argument validation, Debug/Release, quoting, offline integrity, pin updates, direct source edit preservation, no source copying or patch access, clean cache/data preservation")


if __name__ == "__main__":
    main()
