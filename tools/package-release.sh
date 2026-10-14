#!/bin/sh
# Packages a release build of `gamma` as the archive the install script downloads:
#
#     tools/package-release.sh <asset> <path to the gamma binary> <output directory>
#
# writes <output directory>/gamma-<asset>.tar.gz and its .sha256. The archive holds one
# directory, gamma-<version>-<asset>/, laid out as an installation prefix:
#
#     bin/gamma
#     lib/gamma/std/...     the standard library, found relative to the binary
#     LICENSE, README.md
set -eu

if [ $# -ne 3 ]; then
    echo "usage: $0 <asset> <gamma binary> <output directory>" >&2
    exit 2
fi
asset=$1
binary=$2
out=$3
root=$(cd "$(dirname "$0")/.." && pwd)
version=$(sed -n 's/^version = "\(.*\)"$/\1/p' "$root/Cargo.toml" | head -n 1)
name="gamma-$version-$asset"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/$name/bin" "$work/$name/lib/gamma"
cp "$binary" "$work/$name/bin/gamma"
cp -R "$root/lib/std" "$work/$name/lib/gamma/std"
cp "$root/LICENSE" "$root/README.md" "$work/$name/"

mkdir -p "$out"
archive="gamma-$asset.tar.gz"
tar -C "$work" -czf "$out/$archive" "$name"
(
    cd "$out"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$archive" > "$archive.sha256"
    else
        shasum -a 256 "$archive" > "$archive.sha256"
    fi
)
echo "wrote $out/$archive"
