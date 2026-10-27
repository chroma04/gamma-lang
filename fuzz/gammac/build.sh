#!/bin/sh
# Builds libFuzzer executables for the fuzz targets of gammac: fuzz/gammac/build/TARGET.
#
#     fuzz/gammac/build.sh [TARGET...]      # default: every target
#
# The targets are the root modules of this directory (parse, fmt, check, manifest,
# archive). Each is compiled to C by `gammac build --emit-c`, then by clang with coverage
# instrumentation and linked with libFuzzer's driver. Needs clang with libFuzzer (on
# Debian and Ubuntu: `apt install clang libclang-rt-dev`).
#
# Environment:
#   GAMMA    the gamma to use (default: `gamma` on PATH)
#   CLANG    the C compiler (default: clang)
#   MODE     the gammac build mode (default: checked, which keeps the safety checks)
#   SAN      sanitizers for the generated C, as in `-fsanitize=` (default: address; the
#            check target defaults to none because it is slow); `SAN=none` for none
#   OPT      the optimization flag (default: -O1)
#
# The targets are built in parallel.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
: "${GAMMA:=gamma}"
: "${CLANG:=clang}"
: "${MODE:=checked}"
: "${OPT:=-O1}"
# gammac reads the standard library and the runtime of this source tree unless told otherwise.
: "${GAMMA_STD:=$root/lib/std}"
: "${GAMMA_RUNTIME:=$root/runtime}"
export GAMMA_STD GAMMA_RUNTIME

if [ "$#" -eq 0 ]; then set -- parse fmt check manifest archive; fi
out="$here/build"
mkdir -p "$out"

driver=$("$CLANG" --print-file-name=libclang_rt.fuzzer_no_main-x86_64.a)
case "$(uname -m)" in
    aarch64 | arm64) driver=$("$CLANG" --print-file-name=libclang_rt.fuzzer_no_main-aarch64.a) ;;
esac
if [ ! -f "$driver" ]; then
    echo "build.sh: libFuzzer's driver library not found; install clang's runtime (apt install libclang-rt-dev)" >&2
    exit 2
fi

build_one() {
    target=$1
    san=${SAN:-address}
    if [ -z "${SAN:-}" ] && [ "$target" = check ]; then san=none; fi
    if [ "$san" = none ]; then flags=fuzzer-no-link; else flags=fuzzer-no-link,$san; fi
    echo "== $target (sanitizer: $san)"
    "$GAMMA" build --mode="$MODE" --emit-c "$here/$target.gm" -o "$out/$target"
    # The generated C defines `main`; shim.c owns the real one (see there).
    "$CLANG" $OPT -g -w -fno-omit-frame-pointer -fsanitize="$flags" \
        -Dmain=gm_program_main -c "$out/$target.c" -o "$out/$target.o"
    "$CLANG" -fsanitize="$flags" -c "$here/shim.c" -o "$out/$target.shim.o"
    "$CLANG" -fsanitize="$flags" "$out/$target.o" "$out/$target.shim.o" "$driver" \
        -lstdc++ -lm -lpthread -o "$out/$target"
    rm -f "$out/$target.o" "$out/$target.shim.o"
}

for target in "$@"; do
    if [ ! -f "$here/$target.gm" ]; then
        echo "build.sh: no target $target" >&2
        exit 2
    fi
done
# The targets build in parallel (the C compiler takes most of the time); each one's
# messages are shown together when it is done.
pids=""
for target in "$@"; do
    ( build_one "$target" > "$out/$target.log" 2>&1 ) &
    pids="$pids $!"
done
status=0
for pid in $pids; do
    wait "$pid" || status=1
done
for target in "$@"; do
    cat "$out/$target.log"
done
if [ "$status" -ne 0 ]; then echo "build.sh: a build failed" >&2; fi
exit "$status"
