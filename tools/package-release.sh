#!/bin/sh
# Packages a release build of `gamma` as the archive the install script downloads:
#
#     tools/package-release.sh <asset> <path to the gamma binary> <output directory>
#
# writes <output directory>/gamma-<asset>.tar.gz (a .zip for the Windows assets) and its
# .sha256. The archive holds one directory, gamma-<version>-<asset>/, laid out as an
# installation prefix:
#
#     bin/gamma             (bin/gamma.exe on Windows)
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
# The `version` of the `[package]` table (not of another table that may come first).
version=$(awk '/^\[/ { pkg = ($0 == "[package]") } pkg && /^version = / { gsub(/"/, "", $3); print $3; exit }' "$root/Cargo.toml")
[ -n "$version" ] || { echo "$0: no [package] version in Cargo.toml" >&2; exit 1; }
name="gamma-$version-$asset"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/$name/bin" "$work/$name/lib/gamma"
case "$asset" in
    windows-*) exe=gamma.exe ;;
    *) exe=gamma ;;
esac
cp "$binary" "$work/$name/bin/$exe"
cp -R "$root/lib/std" "$work/$name/lib/gamma/std"
cp "$root/LICENSE" "$root/README.md" "$work/$name/"

mkdir -p "$out"
case "$asset" in
    windows-*)
        archive="gamma-$asset.zip"
        rm -f "$out/$archive"
        outabs=$(cd "$out" && pwd)
        if command -v zip >/dev/null 2>&1; then
            (cd "$work" && zip -qr "$outabs/$archive" "$name")
        elif command -v 7z >/dev/null 2>&1; then
            (cd "$work" && 7z a -tzip -bso0 -bsp0 "$outabs/$archive" "$name")
        else
            # bsdtar, which ships with Windows, writes zip archives.
            (cd "$work" && tar -a -cf "$outabs/$archive" "$name")
        fi
        ;;
    *)
        archive="gamma-$asset.tar.gz"
        # The same sources give the same bytes: sorted names, no owners, the time of the
        # last commit (or SOURCE_DATE_EPOCH), and no time stamp in the gzip header.
        epoch=${SOURCE_DATE_EPOCH:-$(git -C "$root" log -1 --format=%ct 2>/dev/null || echo 0)}
        if tar --version 2>/dev/null | grep -q 'GNU tar'; then
            tar -C "$work" --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$epoch" \
                -cf - "$name" | gzip -n -9 > "$out/$archive"
        else
            # bsdtar (macOS): no AppleDouble files, owners zeroed.
            COPYFILE_DISABLE=1 tar -C "$work" --uid 0 --gid 0 -cf - "$name" | gzip -n -9 > "$out/$archive"
        fi
        ;;
esac
(
    cd "$out"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$archive" > "$archive.sha256"
    else
        shasum -a 256 "$archive" > "$archive.sha256"
    fi
)
echo "wrote $out/$archive"
