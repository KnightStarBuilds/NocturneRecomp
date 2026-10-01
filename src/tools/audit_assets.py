#!/usr/bin/env python3
"""Audit owned Nocturne assets in place; never extract or flatten POD archives.

The POD layouts and mount rules follow NearlyTRex/NocturneDecomp commit
872d1d7ce186375df791d12da938a447f51b3216, engine/pod.cpp reconstructions.
This verifies archive structure and mission/cloth references, not gameplay.
"""

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import sys


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical(name):
    # Installed asset names are ASCII; this matches the engine's slash/case rules.
    return name.replace("\\", "/").lower()


def cstring(raw):
    return raw.split(b"\0", 1)[0].decode("cp1252")


def read_archive(path):
    """Read bounded directory metadata; keep payload bytes in their archive."""
    size = path.stat().st_size
    with path.open("rb") as stream:
        header = stream.read(288)
        if len(header) < 84:
            raise ValueError(f"{path.name}: truncated POD header")
        magic = header[:4]
        priority = 1000
        dependencies = []
        if magic == b"POD3":
            if len(header) < 288:
                raise ValueError(f"{path.name}: truncated POD3 header")
            version = 3
            count, directory_offset = struct.unpack_from("<2I", header, 252)
            priority = struct.unpack_from("<i", header, 264)[0]
            names_size, dependency_count = struct.unpack_from("<2I", header, 268)
            entry_size = 20
        elif magic == b"POD2":
            version = 2
            count = struct.unpack_from("<I", header, 88)[0]
            directory_offset, entry_size = 96, 20
            dependency_count = 0
        else:
            version = 1
            count = struct.unpack_from("<I", header)[0]
            directory_offset, entry_size = 84, 40
            dependency_count = 0
        if not 1 <= count <= 99999 or directory_offset + count * entry_size > size:
            raise ValueError(f"{path.name}: invalid directory bounds")
        stream.seek(directory_offset)
        directory = stream.read(count * entry_size)
        names_start = directory_offset + len(directory)
        if version == 2:
            names_size = struct.unpack_from("<I", directory, 8)[0] - names_start
        if version >= 2:
            if not count <= names_size <= count * 256 or names_start + names_size > size:
                raise ValueError(f"{path.name}: invalid filename table bounds")
            names = stream.read(names_size)
        if version == 3:
            if names_start + names_size + dependency_count * 264 > size:
                raise ValueError(f"{path.name}: invalid dependency table bounds")
            for _ in range(dependency_count):
                record = stream.read(264)
                order, required = struct.unpack_from("<2i", record, 256)
                dependencies.append({"filename": cstring(record[:256]),
                                     "mount_order": order, "is_required": required})
        entries = []
        for index in range(count):
            if version == 1:
                record = directory[index * 40:(index + 1) * 40]
                name = cstring(record[:32])
                length, offset = struct.unpack_from("<2I", record, 32)
            else:
                name_offset, length, offset, _, _ = struct.unpack_from("<5I", directory, index * 20)
                if name_offset >= len(names) or b"\0" not in names[name_offset:]:
                    raise ValueError(f"{path.name}: invalid filename offset")
                name = cstring(names[name_offset:])
            normalized = canonical(name)
            if not name or normalized.startswith("/") or ".." in normalized.split("/"):
                raise ValueError(f"{path.name}: unsafe or empty asset name")
            if offset + length > size:
                raise ValueError(f"{path.name}: {name}: payload outside archive")
            entries.append({"path": normalized, "stored_path": name,
                            "archive": path.name, "offset": offset, "bytes": length})
    return {"filename": path.name, "format": version, "entry_count": count,
            "bytes": size, "mount_priority": priority, "dependencies": dependencies,
            "sha256": sha256(path), "entries": entries}


def mount_order(archives, data_dir):
    """Match upstream's sorted file search, stable priority and dependency rules."""
    by_name = {canonical(a["filename"]): a for a in archives}
    ini = next((p for p in data_dir.iterdir() if p.name.lower() == "pod.ini"), None)
    if ini:
        lines = ini.read_text(encoding="cp1252").split()
        if not lines or len(lines) != int(lines[0]) + 1:
            raise ValueError("invalid pod.ini archive count")
        ordered = [by_name[canonical(name)] for name in lines[1:]]
        origin = "pod.ini"
    else:
        # std::sort<string> in core/file_search.cpp compares original name bytes.
        ordered = sorted(archives, key=lambda a: a["filename"])
        origin = "upstream bytewise filename enumeration"
    removed = []
    while True:
        present = {canonical(a["filename"]) for a in ordered}
        missing = [a for a in ordered if any(d["is_required"] and
                   canonical(d["filename"]) not in present for d in a["dependencies"])]
        if not missing:
            break
        removed.extend(a["filename"] for a in missing)
        ordered = [a for a in ordered if a not in missing]
    ordered.sort(key=lambda a: a["mount_priority"])
    # Preserve the pinned reconstructed resolver's use of is_required's sign.
    present = {canonical(a["filename"]) for a in ordered}
    rules = []
    for archive in ordered:
        name = canonical(archive["filename"])
        for dependency in archive["dependencies"]:
            other = canonical(dependency["filename"])
            required = dependency["is_required"]
            if required and other in present:
                rules.append((name, other) if required < 0 else (other, name))
    result = []
    while ordered:
        finished = {canonical(a["filename"]) for a in result}
        candidate = next((a for a in ordered if all(before in finished for before, after
                         in rules if after == canonical(a["filename"]))), None)
        if candidate is None:
            raise ValueError("circular POD dependency")
        result.append(candidate)
        ordered.remove(candidate)
    return result, {"source": origin, "removed_missing_dependencies": removed,
                    "rule": "ascending priority, stable dependency order, first matching archive wins"}


def payload(data_dir, entry):
    with (data_dir / entry["archive"]).open("rb") as stream:
        stream.seek(entry["offset"])
        return stream.read(entry["bytes"])


def verify_manifest(data_dir, manifest_path):
    manifest = json.loads(manifest_path.read_text())
    actual = {p.relative_to(data_dir).as_posix() for p in data_dir.rglob("*") if p.is_file()}
    expected = {entry["path"] for entry in manifest["files"]}
    if actual != expected:
        raise ValueError(f"manifest filename mismatch: missing={expected - actual}, extra={actual - expected}")
    for entry in manifest["files"]:
        path = data_dir / entry["path"]
        if path.stat().st_size != entry["bytes"] or sha256(path) != entry["sha256"]:
            raise ValueError(f"manifest checksum mismatch: {entry['path']}")
    return len(expected)


def audit(data_dir):
    archives = [read_archive(p) for p in sorted(data_dir.iterdir()) if p.suffix.lower() == ".pod"]
    if not archives:
        raise ValueError("no POD archives found")
    ordered, ordering = mount_order(archives, data_dir)
    assets = defaultdict(list)
    for archive in ordered:
        for entry in archive["entries"]:
            assets[entry["path"]].append(entry)
    duplicates = []
    for name, entries in sorted(assets.items()):
        if len(entries) > 1:
            candidates = [{**entry, "sha256": hashlib.sha256(payload(data_dir, entry)).hexdigest()}
                          for entry in entries]
            duplicates.append({"path": name, "winner": entries[0]["archive"],
                               "identical_payloads": len({e["sha256"] for e in candidates}) == 1,
                               "candidates": candidates})
    missions, cloth = [], []
    for name, entries in sorted(assets.items()):
        entry = entries[0]
        if name.endswith(".msn"):
            lines = payload(data_dir, entry).decode("cp1252").splitlines()
            if len(lines) < 8 or lines[0] != "// mission version, actor version":
                raise ValueError(f"invalid mission header: {name}")
            versions = [int(v) for v in lines[1].split(",")]
            set_count = int(lines[5])
            sets = [canonical("models/" + s) for s in lines[6:6 + set_count]]
            script = str(PurePosixPath(name).with_suffix(".scr"))
            if script not in assets or any(s not in assets for s in sets):
                raise ValueError(f"missing script or set for {name}")
            if not payload(data_dir, assets[script][0]).strip():
                raise ValueError(f"empty mission script: {script}")
            missions.append({"path": name, "archive": entry["archive"], "versions": versions,
                             "root_name": lines[3], "sets": sets, "script": script,
                             "sha256": hashlib.sha256(payload(data_dir, entry)).hexdigest()})
        elif name.endswith(".cth"):
            lines = payload(data_dir, entry).decode("cp1252").splitlines()
            if len(lines) < 6 or lines[0] != "version" or lines[2] != "model":
                raise ValueError(f"invalid cloth header: {name}")
            model = canonical("models/" + lines[3])
            if model not in assets:
                raise ValueError(f"missing cloth mesh: {name} -> {model}")
            locks = int(lines[lines.index("lockedVertexCount") + 1])
            lock_start = lines.index("lockedVertexList") + 1
            indices = [int(v) for v in lines[lock_start:lock_start + locks]]
            if len(indices) != locks or any(v < 0 for v in indices):
                raise ValueError(f"invalid locked vertices: {name}")
            collisions = int(lines[lines.index("collideBoneCount") + 1])
            cloth.append({"path": name, "archive": entry["archive"], "version": int(lines[1]),
                          "model": model, "locked_vertex_count": locks,
                          "collision_bone_count": collisions})
    return {"validation_scope": "POD bounds, immutable archive hashes, priority, duplicates, mission headers/set/script presence, cloth headers/mesh/lock lists; no gameplay or solver execution",
            "archive_count": len(archives), "entry_count": sum(a["entry_count"] for a in archives),
            "unique_asset_paths": len(assets), "duplicate_asset_paths": len(duplicates),
            "duplicate_extra_entries": sum(len(es) - 1 for es in assets.values()),
            "differing_duplicate_paths": sum(not d["identical_payloads"] for d in duplicates),
            "mission_count": len(missions), "script_count": sum(p.endswith(".scr") for p in assets),
            "cloth_count": len(cloth), "cloth_versions": dict(Counter(c["version"] for c in cloth)),
            "mount_order": [a["filename"] for a in ordered], "mount_order_rules": ordering,
            "archives": [{k: v for k, v in a.items() if k != "entries"} for a in archives],
            "missions": missions, "cloth": cloth, "duplicates": duplicates}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", type=Path, default=Path("data/original"))
    parser.add_argument("--verify-manifest", type=Path)
    parser.add_argument("--output", type=Path, help="write full JSON report here")
    args = parser.parse_args()
    try:
        report = audit(args.data_dir)
        if args.verify_manifest:
            report["manifest_verified_files"] = verify_manifest(args.data_dir, args.verify_manifest)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps({k: v for k, v in report.items() if k not in
                             {"archives", "missions", "cloth", "duplicates", "mount_order"}}, indent=2))
        else:
            print(json.dumps(report, indent=2))
        return 0
    except (OSError, ValueError, KeyError, IndexError, struct.error) as error:
        print(f"asset audit failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
