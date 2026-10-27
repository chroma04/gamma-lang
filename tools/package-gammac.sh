#!/bin/sh
# Packages a build of `gammac` as the archive the install script downloads with
# GAMMA_ARCHIVE=gammac:
#
#     tools/package-gammac.sh <asset> <path to the gammac binary> <output directory>
#
# writes <output directory>/gammac-<asset>.tar.gz (a .zip for the Windows assets) and its
# .sha256. The archive holds one directory, gammac-<version>-<asset>/, laid out as an
# installation prefix:
#
#     bin/gammac             (bin/gammac.exe on Windows)
#     lib/gamma/std/...      the standard library
#     lib/gamma/core/...     the core library
#     lib/gamma/runtime/...  the C runtime that gammac compiles programs with, and the
#                            debugger helper scripts it copies next to `-g` builds
#     LICENSE, README.md
#
# gammac finds all of these relative to its own executable, so the installation needs no
# environment variables. The version is that of compiler/gamma.toml, the manifest of
# gammac itself.
set -eu

if [ $# -ne 3 ]; then
    echo "usage: $0 <asset> <gammac binary> <output directory>" >&2
    exit 2
fi
asset=$1
binary=$2
out=$3
root=$(cd "$(dirname "$0")/.." && pwd)
# The `version` of the `[package]` table (not of another table that may come first).
version=$(awk '/^\[/ { pkg = ($0 == "[package]") } pkg && /^version = / { gsub(/"/, "", $3); print $3; exit }' "$root/compiler/gamma.toml")
[ -n "$version" ] || { echo "$0: no [package] version in compiler/gamma.toml" >&2; exit 1; }
name="gammac-$version-$asset"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/$name/bin" "$work/$name/lib/gamma/runtime"
case "$asset" in
    windows-*) exe=gammac.exe ;;
    *) exe=gammac ;;
esac
cp "$binary" "$work/$name/bin/$exe"
cp -R "$root/lib/std" "$work/$name/lib/gamma/std"
cp -R "$root/lib/core" "$work/$name/lib/gamma/core"
for f in gm_rt.c gm_rt.h gdb_printers.py lldb_formatters.py; do
    cp "$root/runtime/$f" "$work/$name/lib/gamma/runtime/$f"
done
cp "$root/LICENSE" "$root/README.md" "$work/$name/"

mkdir -p "$out"
case "$asset" in
    windows-*)
        archive="gammac-$asset.zip"
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
        archive="gammac-$asset.tar.gz"
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
