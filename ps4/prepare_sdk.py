#!/usr/bin/env python3
"""Prepare a header overlay for the pinned SDK without changing its source cache."""
import argparse
from pathlib import Path
import re
import sys


def corrected_types(sdk):
    source = sdk / "include/bits/alltypes.h"
    text = source.read_text(encoding="utf-8")
    declarations = re.findall(r"typedef unsigned (int|short) mode_t;", text)
    if not declarations or declarations[0] not in ("int", "short"):
        raise RuntimeError(f"Unrecognized OpenOrbis mode_t declaration in {source}")
    # The v0.5.4 archive declares mode_t twice. Its first, 32-bit declaration
    # overrides the later correct declaration and shifts stat fields by 8 bytes.
    # libkernel and bundled libc++ both use the PS4 16-bit mode_t layout.
    return text.replace("typedef unsigned int mode_t;", "typedef unsigned short mode_t;")


def prepare(sdk, output, check_only=False):
    sdk = sdk.resolve()
    text = corrected_types(sdk)
    if check_only:
        return
    output = output.resolve()
    if output == sdk or output.is_relative_to(sdk):
        raise RuntimeError("SDK overlay must be outside the immutable SDK directory")
    destination = output / "bits/alltypes.h"
    if destination.exists() and destination.read_text(encoding="utf-8") == text:
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()
    try:
        prepare(args.sdk, args.output, args.check_only)
    except (OSError, RuntimeError) as error:
        print(f"PS4 SDK preparation: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
