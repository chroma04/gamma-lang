#!/bin/sh
# Fills fuzz/corpus/{parse,fmt} with the Gamma sources of the repository.
set -eu
cd "$(dirname "$0")/.."
for target in parse fmt; do
    mkdir -p "fuzz/corpus/$target"
    find lib examples tests/conformance -name '*.gm' | while read -r f; do
        cp "$f" "fuzz/corpus/$target/$(echo "$f" | tr '/' '_')"
    done
done
