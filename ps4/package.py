#!/usr/bin/env python3
"""Stage an emulator app0 or build a local homebrew PKG with LibOrbisPkg.

GP4/SFO layout and CLI follow maxton/LibOrbisPkg (GP4/Gp4Project.cs,
SFO/ParamSfo.cs and PkgTool/Program.cs). Original assets are copied verbatim.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


class PackageError(Exception):
    """An actionable preflight or package-tool failure."""


def regular_file(path, nonempty=True):
    if path.is_symlink() or not path.is_file():
        raise PackageError(f"Required regular file is missing or is a symlink: {path}")
    if nonempty and path.stat().st_size == 0:
        raise PackageError(f"Required file is empty: {path}")
    with path.open("rb") as stream:
        stream.read(1)


def read_metadata(path):
    regular_file(path)
    metadata = json.loads(path.read_text(encoding="utf-8"))
    required = {"title", "title_id", "content_id", "app_version", "system_version"}
    if not isinstance(metadata, dict) or set(metadata) != required:
        raise PackageError(f"Package metadata must contain exactly: {', '.join(sorted(required))}")
    if not all(isinstance(value, str) for value in metadata.values()):
        raise PackageError("Package metadata values must be strings")
    if not re.fullmatch(r"[A-Z]{4}[0-9]{5}", metadata["title_id"]):
        raise PackageError("title_id must contain four uppercase letters and five digits")
    if not re.fullmatch(r"[A-Z]{2}[0-9]{4}-" + metadata["title_id"] + r"_00-[A-Z0-9]{16}", metadata["content_id"]):
        raise PackageError("content_id must have 36 characters and contain the matching title_id")
    if not re.fullmatch(r"[0-9]{2}\.[0-9]{2}", metadata["app_version"]):
        raise PackageError("app_version must use the format 01.00")
    title = metadata["title"]
    if not title.strip() or any(ord(char) < 32 for char in title) or len(title.encode("utf-8")) > 127:
        raise PackageError("title must be nonempty UTF-8 text of at most 127 bytes")
    if not re.fullmatch(r"0x[0-9A-Fa-f]{8}", metadata["system_version"]):
        raise PackageError("system_version must be an eight-digit hexadecimal firmware value")
    return metadata


def validate_elf_header(header, path, types):
    if (len(header) != 64 or header[:7] != b"\x7fELF\x02\x01\x01"
            or struct.unpack_from("<H", header, 18)[0] != 62
            or struct.unpack_from("<H", header, 16)[0] not in types
            or struct.unpack_from("<H", header, 52)[0] != 64):
        raise PackageError(f"Not the expected x86-64 PS4 build artifact: {path}")


def validate_executables(elf, eboot):
    regular_file(elf)
    regular_file(eboot)
    # create-fself consumes a conventional ELF, then writes the Orbis ELF in SELF.
    with elf.open("rb") as stream:
        validate_elf_header(stream.read(64), elf, {2, 3})
    with eboot.open("rb") as stream:
        header = stream.read(32)
        if len(header) != 32 or header[:4] != b"\x4f\x15\x3d\x1d":
            raise PackageError(f"Not a PS4 SELF executable: {eboot}; run ./ps4/build.sh first")
        entries = struct.unpack_from("<H", header, 24)[0]
        declared_size = struct.unpack_from("<Q", header, 16)[0]
        elf_offset = 32 + entries * 32
        if entries == 0 or declared_size > eboot.stat().st_size or elf_offset + 64 > declared_size:
            raise PackageError(f"Truncated or invalid PS4 SELF executable: {eboot}")
        stream.seek(elf_offset)
        validate_elf_header(stream.read(64), eboot, {0xFE00, 0xFE10})


def inventory(directory, prefix):
    if directory.is_symlink() or not directory.is_dir():
        raise PackageError(f"Required directory is missing or is a symlink: {directory}")
    result = []
    for current, directories, files in os.walk(directory, followlinks=False):
        directories[:] = sorted(name for name in directories if name != "__MACOSX")
        for name in directories + sorted(files):
            source = Path(current) / name
            if name in {".DS_Store", ".keep"} or name.startswith("._"):
                continue
            mode = source.lstat().st_mode
            if stat.S_ISLNK(mode) or not (stat.S_ISDIR(mode) or stat.S_ISREG(mode)):
                raise PackageError(f"Package inputs must be regular files/directories: {source}")
            relative = source.relative_to(directory)
            if any(ord(char) < 32 or char == "\\" for char in relative.as_posix()):
                raise PackageError(f"Unsupported package path: {source}")
            if stat.S_ISREG(mode):
                regular_file(source, nonempty=False)
                result.append((source, (Path(prefix) / relative).as_posix()))
    return sorted(result, key=lambda entry: entry[1])


def select_tool(root, environment):
    override = environment.get("PS4_PKG_TOOL")
    if override:
        tool = Path(override).expanduser()
        if not tool.is_absolute():
            raise PackageError("PS4_PKG_TOOL must name an absolute executable path")
    else:
        sdk = Path(environment.get("OO_PS4_TOOLCHAIN", str(root / "build/ps4-x86_64/dependency-sources/openorbis/PS4Toolchain"))).expanduser()
        host = {"Darwin": "macos", "Linux": "linux", "Windows": "windows"}.get(platform.system())
        if host is None:
            raise PackageError(f"Unsupported package build host: {platform.system()}")
        tool = sdk / "bin" / host / ("PkgTool.Core.exe" if host == "windows" else "PkgTool.Core")
    if not tool.is_file() or not os.access(tool, os.X_OK):
        raise PackageError(f"PkgTool.Core is missing or not executable: {tool}\nRun ./ps4/build.sh, set OO_PS4_TOOLCHAIN, or set PS4_PKG_TOOL to a host-compatible LibOrbisPkg executable.")
    return tool.resolve()


def run_tool(tool, *arguments):
    try:
        result = subprocess.run([str(tool), *map(str, arguments)], check=False,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    except OSError as error:
        raise PackageError(f"Cannot execute {tool}: {error}. Install a host-compatible LibOrbisPkg tool.") from error
    # Upstream pkg_validate can print failed validations and still return zero.
    if result.returncode or re.search(r"\[ERROR\]|FATAL ERROR|^Error:", result.stdout, re.MULTILINE | re.IGNORECASE):
        raise PackageError(f"PkgTool {arguments[0]} failed ({result.returncode}):\n{result.stdout.strip()}")
    return result.stdout


def encode_sfo(metadata):
    # SFO v1.1 sorted index, UTF-8 strings with capacity, little-endian integers.
    strings = {
        "APP_VER": (metadata["app_version"], 8), "CATEGORY": ("gd", 4),
        "CONTENT_ID": (metadata["content_id"], 48), "FORMAT": ("obs", 4),
        "TITLE": (metadata["title"], 128), "TITLE_ID": (metadata["title_id"], 12),
        "VERSION": (metadata["app_version"], 8),
    }
    # SFO ATTRIBUTE bit 1 assigns Cross to native common-dialog confirmation,
    # matching the game menus. LibOrbisPkg SFO/ParamSfo.cs: AttributeNames[1].
    integers = {"APP_TYPE": 1, "ATTRIBUTE": 0x00000002, "DOWNLOAD_DATA_SIZE": 0,
                "PARENTAL_LEVEL": 1, "SYSTEM_VER": int(metadata["system_version"], 16)}
    keys, values, index = bytearray(), bytearray(), bytearray()
    for key in sorted(strings.keys() | integers.keys()):
        if key in strings:
            value, capacity = strings[key]
            value, kind = value.encode("utf-8") + b"\0", 0x0204
        else:
            value, capacity, kind = struct.pack("<I", integers[key]), 4, 0x0404
        index.extend(struct.pack("<HHIII", len(keys), kind, len(value), capacity, len(values)))
        keys.extend(key.encode("ascii") + b"\0")
        values.extend(value.ljust(capacity, b"\0"))
    key_offset = 20 + len(index)
    keys.extend(b"\0" * (-(key_offset + len(keys)) % 4))
    return struct.pack("<4sIIII", b"\0PSF", 0x101, key_offset, key_offset + len(keys), len(strings) + len(integers)) + index + keys + values


def write_gp4(path, stage, metadata, targets):
    project = ET.Element("psproject", fmt="gp4", version="1000")
    volume = ET.SubElement(project, "volume")
    ET.SubElement(volume, "volume_type").text = "pkg_ps4_app"
    ET.SubElement(volume, "volume_id").text = "PS4VOLUME"
    ET.SubElement(volume, "volume_ts").text = "2020-01-01 00:00:00"
    ET.SubElement(volume, "package", content_id=metadata["content_id"], passcode="0" * 32,
                  storage_type="digital50", app_type="full")
    chunks = ET.SubElement(volume, "chunk_info", chunk_count="1", scenario_count="1")
    ET.SubElement(ET.SubElement(chunks, "chunks"), "chunk", id="0", layer_no="0", label="Nocturne")
    ET.SubElement(ET.SubElement(chunks, "scenarios", default_id="0"), "scenario",
                  id="0", type="sp", initial_chunk_count="1", label="Nocturne").text = "0"
    files = ET.SubElement(project, "files", img_no="0")
    rootdir = ET.SubElement(project, "rootdir")
    directories = {".": rootdir}
    for target in sorted(targets):
        ET.SubElement(files, "file", targ_path=target, orig_path=str(stage / target))
        parent = Path(target).parent
        for directory in reversed([parent, *parent.parents]):
            if directory.as_posix() not in directories:
                directories[directory.as_posix()] = ET.SubElement(
                    directories[directory.parent.as_posix()], "dir", targ_name=directory.name)
    ET.indent(project, space="  ")
    ET.ElementTree(project).write(path, encoding="utf-8", xml_declaration=True)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def prepare_inputs(root, config, data_directory):
    """Validate common app0 inputs without creating or replacing output."""
    build = root / "build/ps4-x86_64"
    metadata = read_metadata(root / "ps4/package.json")
    elf, eboot = (build / "bin" / config / name for name in ("nocturne.elf", "eboot.bin"))
    validate_executables(elf, eboot)
    data_directory = data_directory.expanduser().absolute()
    inputs = inventory(data_directory, "data/original")
    pods = [source for source, _ in inputs if source.suffix.lower() == ".pod"]
    # Runtime discovery scans the selected directory itself; nested archives
    # remain assets but cannot make a parent installation folder bootable.
    if not any(source.parent == data_directory for source in pods):
        raise PackageError(f"No POD archives found directly in {data_directory}; select the directory containing the original game POD files")
    for pod in pods:
        regular_file(pod)
    resources = build / "resources" / config
    regular_file(resources / "fonts/Fallback.ttf")
    regular_file(resources / "gamecontrollerdb.txt")
    # Firmware startup needs both OpenOrbis stubs before main, even though
    # the executable uses the SDK's statically linked C runtime.
    for module in ("libc.prx", "libSceFios2.prx"):
        regular_file(resources / "sce_module" / module)
    if not inventory(resources / "licenses", "licenses"):
        raise PackageError(f"Dependency license notices are missing: {resources / 'licenses'}")
    inputs += inventory(resources, "")
    icon = root / "ps4/resources/icon0.png"
    regular_file(icon)
    with icon.open("rb") as stream:
        icon_header = stream.read(24)
    if icon_header[:8] != b"\x89PNG\r\n\x1a\n" or icon_header[16:24] != struct.pack(">II", 512, 512):
        raise PackageError(f"Expected a 512 x 512 PNG package icon: {icon}")
    icon_notice = root / "ps4/resources/icon-license.txt"
    regular_file(icon_notice)
    inputs += [(eboot, "eboot.bin"), (icon, "sce_sys/icon0.png"), (icon_notice, "licenses/ps4-icon-license.txt")]
    seen = {}
    for target in ["sce_sys/param.sfo", "package-manifest.json", *(target for _, target in inputs)]:
        path = Path(target)
        for entry in [*reversed(path.parents), path]:
            if entry == Path("."):
                continue
            name, directory = entry.as_posix(), entry != path
            previous = seen.get(name.casefold())
            if previous and (previous != (name, directory) or not directory):
                raise PackageError(f"Duplicate or case-ambiguous package path: {name}")
            seen[name.casefold()] = (name, directory)
    return metadata, elf, data_directory, inputs


def validate_output_directories(root, data_directory, *directories):
    data_directory = data_directory.resolve()
    for directory in directories:
        for parent in (directory, *directory.parents):
            if parent == root:
                break
            if parent.is_symlink():
                raise PackageError(f"Build output directories must not be symlinks: {parent}")
            if parent.exists() and not parent.is_dir():
                raise PackageError(f"Build output path is not a directory: {parent}")
        if data_directory.is_relative_to(directory) or directory.is_relative_to(data_directory):
            raise PackageError("Original assets must not overlap output or staging directories")


def materialize_app0(stage, config, metadata, elf, inputs, packager=None):
    """Use identical runtime files and manifest fields for staging and PKGs."""
    stage.mkdir()
    for source, target in inputs:
        destination = stage / target
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    (stage / "sce_sys/param.sfo").write_bytes(encode_sfo(metadata))
    targets = [target for _, target in inputs] + ["sce_sys/param.sfo"]
    manifest = {"format": 1, "configuration": config, "metadata": metadata,
                "elf_sha256": sha256(elf), "packager": packager,
                "files": {target: {"size": (stage / target).stat().st_size,
                                    "sha256": sha256(stage / target)} for target in sorted(targets)}}
    (stage / "package-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return targets + ["package-manifest.json"]


def stage_app0(root, config, data_directory):
    root = root.resolve()
    build = root / "build/ps4-x86_64"
    metadata, elf, data_directory, inputs = prepare_inputs(root, config, data_directory)
    stage_parent = build / "stage" / config
    output = build / "app0" / config
    validate_output_directories(root, data_directory, stage_parent, output)

    stage_parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="app0-", dir=stage_parent))
    candidate, backup = work / "app0", work / "previous-app0"
    preserve_backup = False
    try:
        materialize_app0(candidate, config, metadata, elf, inputs)
        output.parent.mkdir(parents=True, exist_ok=True)
        # A portable rename cannot replace a nonempty directory. Retain the
        # previous tree until the complete candidate has been published.
        try:
            if output.exists():
                os.replace(output, backup)
            os.replace(candidate, output)
        except BaseException:
            if backup.exists():
                try:
                    os.replace(backup, output)
                except BaseException as error:
                    preserve_backup = True
                    raise PackageError(f"Cannot restore previous app0; its files remain at {backup}: {error}") from error
            raise
    finally:
        # A failed restoration must never lose the only previous good tree.
        if not preserve_backup:
            shutil.rmtree(work)
    print(f"Staged {config} app0 at {output}")
    return output


def package(root, config, data_directory, environment):
    root = root.resolve()
    build = root / "build/ps4-x86_64"
    metadata, elf, data_directory, inputs = prepare_inputs(root, config, data_directory)
    stage_parent = build / "stage" / config
    output_directory = build / "pkg" / config
    validate_output_directories(root, data_directory, stage_parent, output_directory)
    tool = select_tool(root, environment)
    tool_version = run_tool(tool, "version").strip()
    if "PkgTool" not in tool_version or "LibOrbisPkg" not in tool_version:
        raise PackageError(f"Executable does not identify itself as LibOrbisPkg/PkgTool: {tool}")

    # Everything above is read-only. A failed build never replaces the last PKG.
    stage_parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="package-", dir=stage_parent) as temporary:
        work = Path(temporary)
        stage, candidate_directory = work / "app0", work / "pkg"
        candidate_directory.mkdir()
        targets = materialize_app0(stage, config, metadata, elf, inputs, tool_version)
        project = work / "nocturne.gp4"
        write_gp4(project, stage, metadata, targets)
        print(f"Packaging {config}: {len(targets)} files from {data_directory}", flush=True)
        run_tool(tool, "pkg_build", project, candidate_directory)
        candidate = candidate_directory / (metadata["content_id"] + ".pkg")
        regular_file(candidate)
        with candidate.open("rb") as stream:
            if stream.read(4) != b"\x7fCNT":
                raise PackageError("PkgTool output does not have a PS4 package header")
        validation = run_tool(tool, "pkg_validate", candidate)
        if "[OK]" not in validation:
            raise PackageError("PkgTool did not report any successful package validation checks")
        output_directory.mkdir(parents=True, exist_ok=True)
        output = output_directory / candidate.name
        os.replace(candidate, output)
        print(f"Created {output}\nSHA256 {sha256(output)}")
        return output


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--config", choices=("Debug", "Release"), default="Debug")
    parser.add_argument("--data-dir", type=Path, help="original game assets (default: data/original)")
    parser.add_argument("--stage-only", action="store_true", help="refresh app0/<Config> for an emulator without running PkgTool")
    arguments = parser.parse_args(argv)
    root = Path(__file__).resolve().parent.parent
    try:
        data_directory = arguments.data_dir or root / "data/original"
        if arguments.stage_only:
            stage_app0(root, arguments.config, data_directory)
        else:
            package(root, arguments.config, data_directory, os.environ)
    except (PackageError, OSError, ValueError) as error:
        print(f"PS4 packaging error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
