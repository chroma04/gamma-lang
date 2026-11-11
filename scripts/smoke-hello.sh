#!/bin/sh
# Runs a small program that imports the standard library with the given gamma and checks
# its output.
#
#     scripts/smoke-hello.sh GAMMA
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 GAMMA" >&2; exit 2; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
printf 'm :: import "std/math"\nmain :: fn() needs Out { print("{}\\n", m.abs(-3)) }\n' > "$work/hello.gm"
out=$("$1" run "$work/hello.gm")
[ "$out" = 3 ] || { echo "$0: the program printed '$out', not 3" >&2; exit 1; }
