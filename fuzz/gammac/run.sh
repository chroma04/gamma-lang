#!/bin/sh
# Runs one fuzz target built by build.sh, with the options of its kind; further arguments
# go to libFuzzer.
#
#     fuzz/gammac/run.sh TARGET [LIBFUZZER_OPTION...]
#     fuzz/gammac/run.sh parse -max_total_time=600 -jobs=4 -workers=4
#
# New inputs go to fuzz/gammac/corpus/TARGET; fuzz/corpus/TARGET (from seed.sh) is read as
# well. A crashing input is saved as fuzz/gammac/artifacts/TARGET/crash-* (a trap shows
# as "fuzz target exited" with status 134 on the trap's own message). Reproduce with
# `fuzz/gammac/run.sh TARGET FILE`.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
if [ "$#" -lt 1 ]; then
    echo "usage: run.sh TARGET [LIBFUZZER_OPTION...]" >&2
    exit 2
fi
target=$1
shift
exe="$here/build/$target"
if [ ! -x "$exe" ]; then
    echo "run.sh: $exe is missing; run build.sh $target first" >&2
    exit 2
fi
# The compiler under test reads the standard library from the source tree.
: "${GAMMA_STD:=$root/lib/std}"
: "${GAMMA_RUNTIME:=$root/runtime}"
export GAMMA_STD GAMMA_RUNTIME
# Leaks do not matter here: every input runs in an arena, and the fuzzer's own state lives on.
: "${ASAN_OPTIONS:=detect_leaks=0:abort_on_error=0:symbolize=1}"
export ASAN_OPTIONS

case "$target" in
    parse | fmt) options="-timeout=5 -rss_limit_mb=4096" ;;
    manifest) options="-timeout=5 -rss_limit_mb=4096" ;;
    archive) options="-timeout=20 -rss_limit_mb=4096 -max_len=65536" ;;
    check) options="-timeout=20 -rss_limit_mb=4096 -max_len=8192" ;;
    *) options="" ;;
esac

# A file argument reproduces that input instead of fuzzing.
for arg in "$@"; do
    if [ -f "$arg" ]; then
        exec "$exe" "$@"
    fi
done

mkdir -p "$here/corpus/$target" "$here/artifacts/$target"
# shellcheck disable=SC2086
exec "$exe" $options -artifact_prefix="$here/artifacts/$target/" "$@" \
    "$here/corpus/$target" "$root/fuzz/corpus/$target"
