#!/bin/sh
# Package an existing PS4 cross-build; argument validation happens before staging.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$script_dir/package.py" "$@"
