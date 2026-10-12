#!/bin/sh
# Build (and optionally test) from Windows via WSL:  wsl -d Ubuntu -- sh /mnt/c/path/to/skiff/wsl-build.sh [test] [build.sh args]
# Uses a Python venv at ~/.venv-fm1 (Pillow + fonttools; see BUILDING.md).
set -e
cd "$(dirname "$0")"
export PATH="$HOME/.venv-fm1/bin:$PATH"
if [ "$1" = test ]; then shift; sh build.sh "$@" && sh tests/run_tests.sh; else sh build.sh "$@"; fi
