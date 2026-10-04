#!/usr/bin/env python3
"""Verify external dependency archives and prepare disposable build copies.

The game's maintained source lives in src/ and is not managed by this script.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "deps/cache"


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def unpack(archive, destination):
    # Archive path/link validation prevents downloads escaping their cache root.
    with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
        directory = Path(temporary)
        with tarfile.open(archive) as source:
            for member in source.getmembers():
                if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
                    raise RuntimeError(f"Unsupported archive member: {member.name}")
                target = (directory / member.name).resolve()
                if not target.is_relative_to(directory):
                    raise RuntimeError(f"Unsafe archive path: {member.name}")
                if member.issym() or member.islnk():
                    base = target.parent if member.issym() else directory
                    if not (base / member.linkname).resolve().is_relative_to(directory):
                        raise RuntimeError(f"Unsafe archive link: {member.name}")
            # Apple Command Line Tools still ship Python 3.9 without tar filters.
            source.extractall(directory)
        entries = list(directory.iterdir())
        if len(entries) != 1 or not entries[0].is_dir():
            raise RuntimeError(f"Expected one archive root in {archive}")
        shutil.move(str(entries[0]), destination)


def tree_digest(root):
    result = hashlib.sha256()
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        # tar implementations apply different umasks to writable bits. Only
        # executable bits affect source semantics and should enter provenance.
        mode = path.lstat().st_mode & 0o111
        if path.is_symlink():
            entry = [relative, "link", mode, os.readlink(path)]
        elif path.is_dir():
            entry = [relative, "directory", mode]
        elif path.is_file():
            entry = [relative, "file", mode, digest(path)]
        else:
            raise RuntimeError(f"Unexpected cache object: {path}")
        result.update(json.dumps(entry, ensure_ascii=True).encode())
        result.update(b"\n")
    return result.hexdigest()


def verified_source(name, archive, expected):
    cached = CACHE / name
    marker = CACHE / f".{name}-source.json"
    provenance = json.loads(marker.read_text()) if marker.exists() else {}
    if cached.exists() and provenance.get("archive_sha256") == expected:
        if tree_digest(cached) != provenance.get("tree_sha256"):
            raise RuntimeError(f"Immutable source cache changed: {cached}; restore it from its verified archive")
        return cached
    # A changed pin must never reuse old extracted sources. A legacy cache with
    # no marker is adopted only after comparing it with the verified archive.
    with tempfile.TemporaryDirectory(dir=CACHE) as temporary:
        candidate = Path(temporary) / name
        unpack(archive, candidate)
        fingerprint = tree_digest(candidate)
        if cached.exists() and not provenance:
            if tree_digest(cached) != fingerprint:
                raise RuntimeError(f"Unverified source cache differs from archive: {cached}")
        else:
            if cached.exists():
                shutil.rmtree(cached)
            shutil.move(str(candidate), cached)
        marker.write_text(json.dumps({"archive_sha256": expected, "tree_sha256": fingerprint}) + "\n")
    return cached


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--verify-cache-only", action="store_true")
    parser.add_argument("--target", choices=("desktop", "ps4"), default="desktop",
                        help="Select target-specific dependencies (default: desktop)")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if build.parent != ROOT / "build":
        parser.error("build directory must be immediately below this project's build/")
    CACHE.mkdir(parents=True, exist_ok=True)
    generated = build / "dependency-sources"
    if not args.verify_cache_only:
        build.mkdir(parents=True, exist_ok=True)
        generated.mkdir(exist_ok=True)
    manifest = json.loads((ROOT / "deps/manifest.json").read_text())
    for dependency in manifest["dependencies"]:
        if args.target not in dependency.get("targets", ["desktop", "ps4"]):
            continue
        name = dependency["name"]
        archive = CACHE / dependency["archive"]
        expected = dependency["sha256"]
        if not archive.exists():
            if args.offline:
                raise RuntimeError(f"Offline: missing {archive}")
            partial = archive.with_suffix(archive.suffix + ".partial")
            subprocess.run(["curl", "--fail", "--location", "--retry", "3",
                            dependency["url"], "--output", str(partial)], check=True)
            if digest(partial) != expected:
                partial.unlink()
                raise RuntimeError(f"SHA256 mismatch for {name}")
            partial.replace(archive)
        if digest(archive) != expected:
            raise RuntimeError(f"SHA256 mismatch for {archive}; cache was not changed")
        cached = verified_source(name, archive, expected)
        if args.verify_cache_only:
            continue
        destination = generated / name
        fingerprint = hashlib.sha256(expected.encode())
        stamp = build / f".{name}-prepared"
        if destination.exists() and stamp.exists() and stamp.read_text() == fingerprint.hexdigest():
            continue
        # Dependency builds may generate files, so keep their verified cache
        # separate from the disposable working copy.
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(cached, destination, symlinks=True)
        stamp.write_text(fingerprint.hexdigest())
        print(f"Prepared {name} {dependency['version']}", flush=True)


if __name__ == "__main__":
    main()
