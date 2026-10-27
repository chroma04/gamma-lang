#!/bin/sh
# Checks an installed gammac (the contents of a gammac archive, unpacked):
#
#     tools/smoke-gammac.sh <path to the installed bin/gammac>
#
# It starts gammac through PATH from an unrelated directory, without GAMMA_STD,
# GAMMA_CORE or GAMMA_RUNTIME, so that everything it needs must be found from its own
# location: the libraries and the runtime in lib/gamma next to bin. It checks the version,
# runs a program that imports `std/...` on the interpreter and through C, and builds an
# executable. The last two need a C compiler (cc, gcc or clang; MinGW-w64 on Windows).
set -eu

if [ $# -ne 1 ]; then
    echo "usage: $0 <installed gammac>" >&2
    exit 2
fi
exe=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
bin=$(dirname "$exe")
name=$(basename "$exe" .exe)

unset GAMMA_STD GAMMA_CORE GAMMA_RUNTIME
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/hello.gm" <<'EOF'
str :: import "std/str"

main :: fn() needs Out {
    print("{}\n", str.contains("gamma", "mm"))
}
EOF
cd "$work"
PATH="$bin:$PATH"
export PATH

# Windows prints \r\n.
run() { "$@" | tr -d '\r'; }
check() {
    got=$(run "$@")
    if [ "$got" != true ]; then
        echo "smoke test: \`$*\` printed '$got', expected 'true'" >&2
        exit 1
    fi
}

"$name" version
check "$name" run --backend=interp hello.gm
check "$name" run --backend=c hello.gm
"$name" build hello.gm -o hello
check ./hello
echo "smoke test passed"
