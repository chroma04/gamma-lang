#!/bin/sh
# Checks that the seed is what it claims to be: the C that the compiler built from it
# generates for the sources that bootstrap/seed/SOURCE names (git tree ids of compiler/,
# lib/ and runtime/). A seed that was edited, or generated from other sources, fails.
#
#   bootstrap/verify.sh [GAMMA0]       (default: target/bootstrap/gamma0, from build.sh)
#
# The seed is generated on x86-64 Linux, so this check runs there. It needs the git
# history that holds those trees.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
gamma0=${1:-$root/target/bootstrap/gamma0}
case "$gamma0" in /*) ;; *) gamma0=$(pwd)/$gamma0 ;; esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/src" "$work/out"
while read -r dir tree; do
    mkdir -p "$work/src/$dir"
    git -C "$root" archive "$tree" | tar -x -C "$work/src/$dir"
done < "$here/seed/SOURCE"

cd "$work/src"
GAMMA_STD=lib/std GAMMA_RUNTIME=runtime GAMMA_CACHE_DIR= GAMMA_C_UNITS=8 \
    "$gamma0" build compiler --emit-c -o "$work/out/gammac"
cp "$here/seed/SOURCE" "$work/out/SOURCE"
if diff -r "$here/seed" "$work/out" > /dev/null; then
    echo "verify: the seed is the compiler's C for the sources it names"
else
    echo "verify: the seed differs from what its compiler generates for the sources it names" >&2
    diff -rq "$here/seed" "$work/out" >&2 || true
    exit 1
fi
