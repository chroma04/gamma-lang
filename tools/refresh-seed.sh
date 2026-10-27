#!/bin/sh
# Regenerates bootstrap/seed from the committed sources of compiler/, lib/ and runtime/:
# builds their compiler, has it build itself, and writes the C that this compiler generates
# for itself, split into units, with the git trees of the sources in bootstrap/seed/SOURCE.
#
#   tools/refresh-seed.sh [GAMMA]
#
# GAMMA is any compiler that can build the current sources (by default the one
# bootstrap/build.sh makes from the current seed). A change that makes the compiler use a
# language feature the seed's compiler lacks therefore comes in two steps: first the
# compiler learns the feature and the seed is refreshed, then its sources use it.
set -eu

root=$(git rev-parse --show-toplevel)
cd "$root"
if ! git diff --quiet HEAD -- compiler lib runtime || [ -n "$(git ls-files --others --exclude-standard compiler lib runtime)" ]; then
    echo "refresh-seed: commit the changes to compiler/, lib/ and runtime/ first" >&2
    exit 1
fi

out=$root/target/refresh-seed
rm -rf "$out"
mkdir -p "$out/c"
# Relative paths: the names of the standard library's files end up in the C.
GAMMA_STD=lib/std
GAMMA_RUNTIME=runtime
GAMMA_CACHE_DIR=$out/cache
export GAMMA_STD GAMMA_RUNTIME GAMMA_CACHE_DIR

if [ $# -ge 1 ]; then
    start=$1
else
    bootstrap/build.sh "$out/bootstrap"
    start=$out/bootstrap/gamma
fi
# The compiler of the current sources, built by a compiler of the current sources.
"$start" build compiler -o "$out/stage1"
"$out/stage1" build compiler -o "$out/stage2"
GAMMA_C_UNITS=8 "$out/stage2" build compiler --emit-c -o "$out/c/gammac"

rm -f bootstrap/seed/*.c bootstrap/seed/*.h
cp "$out"/c/*.c "$out"/c/*.h bootstrap/seed/
{
    printf 'compiler %s\n' "$(git rev-parse HEAD:compiler)"
    printf 'lib %s\n' "$(git rev-parse HEAD:lib)"
    printf 'runtime %s\n' "$(git rev-parse HEAD:runtime)"
} > bootstrap/seed/SOURCE
echo "refresh-seed: bootstrap/seed now holds the compiler's C for $(git rev-parse --short HEAD)"
