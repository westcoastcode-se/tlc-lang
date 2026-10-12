#!/bin/sh
# Locate tlc and run, check, or write a debug binary.
# Usage: tlc.sh run|check|build-debug <file.tlc> [output]
set -eu

mode=${1:?}
file=${2:?}
out=${3:-}

root=${ZED_WORKTREE_ROOT:-}
if [ -n "$root" ] && [ -x "$root/build/tlc" ]; then
    bin=$root/build/tlc
elif command -v tlc >/dev/null 2>&1; then
    bin=$(command -v tlc)
else
    echo "tlc was not found. Build it with: cmake --build build --target tlc" >&2
    exit 1
fi

case "$mode" in
run)
    exec "$bin" --run "$file"
    ;;
check)
    exec "$bin" --check "$file"
    ;;
build-debug)
    if [ -z "$out" ]; then
        echo "build-debug needs an output path" >&2
        exit 2
    fi
    mkdir -p "$(dirname "$out")"
    "$bin" -g -o "$out" "$file"
    ;;
*)
    echo "unknown mode: $mode" >&2
    exit 2
    ;;
esac
