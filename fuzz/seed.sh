#!/bin/sh
# Fills fuzz/corpus/{parse,fmt,check} with the Gamma sources of the repository: the parse
# and fmt targets get the standard library too, the check target (which loads the standard
# library itself) only the programs of the examples and the conformance tests.
set -eu
cd "$(dirname "$0")/.."
for target in parse fmt check; do
    mkdir -p "fuzz/corpus/$target"
    case "$target" in
        check) roots="examples tests/conformance" ;;
        *) roots="lib examples tests/conformance" ;;
    esac
    find $roots -name '*.gm' | while read -r f; do
        cp "$f" "fuzz/corpus/$target/$(echo "$f" | tr '/' '_')"
    done
done
