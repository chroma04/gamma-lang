#!/bin/sh
# Prints the compiler's package version: the `version` of the `[package]` table of
# compiler/gamma.toml (not of another table that may come first).
#
#     scripts/version.sh
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
version=$(awk '/^\[/ { pkg = ($0 == "[package]") } pkg && /^version = / { gsub(/"/, "", $3); print $3; exit }' "$root/compiler/gamma.toml")
[ -n "$version" ] || { echo "$0: no [package] version in compiler/gamma.toml" >&2; exit 1; }
echo "$version"
