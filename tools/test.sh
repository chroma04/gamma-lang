#!/bin/sh
# Builds `gamma` from the current sources and runs the test suites with it:
#
#     tools/test.sh [harness arguments: -j N, -v, --list, PATTERN...]
#
# The suites (tests/harness) test the executable that GAMMA_TEST_COMPILER names. Unless it
# is set, this script builds target/bootstrap/gamma: the first time from the seed
# (bootstrap/build.sh, a few minutes), afterwards with the previous target/bootstrap/gamma
# and its cache of compiled units, and only when compiler/, lib/ or runtime/ changed.
# The harness is built with that compiler for the host, whatever CC and CFLAGS say (they
# may name a cross compiler that the tests are about), unless GAMMA_TEST_HARNESS names a
# harness executable built beforehand.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
out=$root/target/bootstrap
if [ -z "${GAMMA_TEST_COMPILER:-}" ]; then
    stamp=$(find compiler lib runtime -type f | LC_ALL=C sort | xargs cat | cksum)
    if [ ! -x "$out/gamma" ]; then
        bootstrap/build.sh "$out"
        echo "$stamp" > "$out/gamma.sources"
    elif [ "$stamp" != "$(cat "$out/gamma.sources" 2>/dev/null)" ]; then
        echo "test: rebuilding $out/gamma from the current sources"
        GAMMA_STD=$root/lib/std GAMMA_RUNTIME=$root/runtime GAMMA_CACHE_DIR=$out/cache \
            "$out/gamma" build compiler -o "$out/gamma.next"
        mv "$out/gamma.next" "$out/gamma"
        echo "$stamp" > "$out/gamma.sources"
    fi
    GAMMA_TEST_COMPILER=$out/gamma
    export GAMMA_TEST_COMPILER
fi
if [ -z "${GAMMA_TEST_HARNESS:-}" ]; then
    mkdir -p "$out"
    env -u CC -u CFLAGS -u LDFLAGS -u GAMMA_RUNNER -u GAMMA_C_UNITS \
        GAMMA_STD="$root/lib/std" GAMMA_RUNTIME="$root/runtime" GAMMA_CACHE_DIR="$out/cache" \
        "$GAMMA_TEST_COMPILER" build tests/harness -o "$out/harness"
    GAMMA_TEST_HARNESS=$out/harness
fi
exec "$GAMMA_TEST_HARNESS" "$@"
