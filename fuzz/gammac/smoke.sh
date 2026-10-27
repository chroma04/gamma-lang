#!/bin/sh
# Builds every fuzz target (or the ones named) and runs each for N seconds on the seed
# corpus; fails if any run finds a crash, a hang or a blowup.
#
#     fuzz/gammac/smoke.sh [SECONDS] [TARGET...]      # default: 30 seconds, every target
#
# Environment: NO_BUILD=1 reuses the executables of an earlier build; the variables of
# build.sh (GAMMAC, CLANG, SAN, ...) and run.sh apply. Crashing inputs are left in
# fuzz/gammac/artifacts/TARGET/.
set -u
here=$(cd "$(dirname "$0")" && pwd)
seconds=${1:-30}
if [ "$#" -gt 0 ]; then shift; fi
if [ "$#" -eq 0 ]; then set -- parse fmt check manifest archive; fi

"$here/seed.sh" || exit 2
if [ -z "${NO_BUILD:-}" ]; then
    "$here/build.sh" "$@" || exit 2
fi

failed=""
for target in "$@"; do
    echo "== fuzzing $target for ${seconds}s"
    # A fresh corpus directory per run keeps the result independent of earlier runs.
    rm -rf "$here/corpus/$target" "$here/artifacts/$target"
    if ! "$here/run.sh" "$target" -max_total_time="$seconds" -print_final_stats=1; then
        echo "== $target: FAILED"
        failed="$failed $target"
    fi
done
if [ -n "$failed" ]; then
    echo "fuzz smoke test failed for:$failed" >&2
    exit 1
fi
echo "fuzz smoke test passed"
