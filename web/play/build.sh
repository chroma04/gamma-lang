#!/bin/sh
# Builds the playground's interpreter from the compiler's sources:
#
#   web/play/build.sh [OUT_DIR]
#
# writes OUT_DIR/gamma-play.wasm (a WASI command: `run` or `fmt` with the program on
# standard input) and OUT_DIR/gamma-play-libs.json (the standard and core libraries it
# reads, which web/static/play-worker.js serves from memory). OUT_DIR defaults to
# web/play/target.
#
# Needs gamma (GAMMA, or `gamma` on PATH), a C compiler for wasm32-wasi (clang with
# the WASI C library: WASI_SDK_PATH, or CC="clang --target=wasm32-wasi") and Binaryen's
# wasm-opt (WASM_OPT), because the interpreter switches fibers. See "Targets, modes and
# caches" in docs/self-hosting.md. GAMMA_STD and GAMMA_RUNTIME default to this checkout's.
#
# MODE (default checked) is the build mode of the interpreter itself; the programs
# it runs are interpreted the same way in every mode.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=${1:-$here/target}
gamma=${GAMMA:-gamma}
: "${GAMMA_STD:=$root/lib/std}"
: "${GAMMA_RUNTIME:=$root/runtime}"
export GAMMA_STD GAMMA_RUNTIME

mkdir -p "$out"

# The libraries, one JSON object from path (/std/..., /core/...) to text.
"$gamma" run --backend=interp "$here/tools/bundle.gm" "$out/gamma-play-libs.json" \
    "/std=$GAMMA_STD" "/core=$(dirname "$GAMMA_STD")/core"

# The interpreter. gamma writes the C next to the module; it is not needed afterwards.
"$gamma" build --mode="${MODE:-checked}" --target=wasm32-wasi "$here" -o "$out/gamma-play.wasm"
rm -f "$out"/gamma-play.wasm.c "$out"/gamma-play.wasm.*.c "$out"/gamma-play.wasm.*.h

ls -l "$out/gamma-play.wasm" "$out/gamma-play-libs.json"
