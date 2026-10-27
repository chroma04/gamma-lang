#!/bin/sh
# Installs a prebuilt `gamma` release on Linux or macOS (x86_64 or arm64):
#
#     curl -fsSL https://github.com/chroma04/gamma-lang/releases/latest/download/install.sh | sh
#
# Environment:
#   GAMMA_VERSION   the release to install, such as `v0.1.0` (default: the latest)
#   GAMMA_INSTALL   the installation prefix (default: ~/.gamma); the compiler goes to
#                   $GAMMA_INSTALL/bin/gamma, its libraries to
#                   $GAMMA_INSTALL/lib/gamma/<name>
#   GAMMA_DOWNLOAD  the URL of the directory holding the release archives (default: the
#                   GitHub release of GAMMA_VERSION)
set -eu

repo="https://github.com/chroma04/gamma-lang"
prefix="${GAMMA_INSTALL:-$HOME/.gamma}"

say() { printf '%s\n' "$*" >&2; }
fail() { say "install.sh: $*"; exit 1; }

case "$(uname -s)" in
    Linux) os=linux ;;
    Darwin) os=macos ;;
    *) fail "no prebuilt gamma for $(uname -s); build it from source: $repo" ;;
esac
case "$(uname -m)" in
    x86_64 | amd64) arch=x86_64 ;;
    aarch64 | arm64) arch=aarch64 ;;
    *) fail "no prebuilt gamma for $(uname -m); build it from source: $repo" ;;
esac
product=gamma
asset="$product-$os-$arch.tar.gz"

if [ -n "${GAMMA_DOWNLOAD:-}" ]; then
    base=$GAMMA_DOWNLOAD
    # Over plain HTTP the archive and its checksum could both be replaced in transit.
    case "$base" in
        https://* | file://*) ;;
        *) fail "GAMMA_DOWNLOAD must be an https:// or file:// URL" ;;
    esac
elif [ -n "${GAMMA_VERSION:-}" ]; then
    base="$repo/releases/download/$GAMMA_VERSION"
else
    base="$repo/releases/latest/download"
fi

if command -v curl >/dev/null 2>&1; then
    fetch() { curl --proto '=https,file' --proto-redir '=https' -fsSL -o "$2" "$1"; }
elif command -v wget >/dev/null 2>&1; then
    fetch() { wget --https-only -q -O "$2" "$1"; }
else
    fail "needs curl or wget"
fi
if command -v sha256sum >/dev/null 2>&1; then
    sha256() { sha256sum "$1" | cut -d ' ' -f 1; }
elif command -v shasum >/dev/null 2>&1; then
    sha256() { shasum -a 256 "$1" | cut -d ' ' -f 1; }
else
    fail "needs sha256sum or shasum to check the download"
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

say "Downloading $base/$asset"
fetch "$base/$asset" "$work/$asset" || fail "cannot download $base/$asset"
fetch "$base/$asset.sha256" "$work/$asset.sha256" || fail "cannot download $base/$asset.sha256"
expected=$(cut -d ' ' -f 1 < "$work/$asset.sha256")
case "$expected" in
    *[!0-9a-f]* | "") fail "$asset.sha256 does not hold a SHA-256" ;;
esac
[ "${#expected}" -eq 64 ] || fail "$asset.sha256 does not hold a SHA-256"
actual=$(sha256 "$work/$asset")
[ "$expected" = "$actual" ] || fail "checksum mismatch for $asset (expected $expected, got $actual)"

tar -C "$work" -xzf "$work/$asset"
src=$(echo "$work"/"$product"-*-"$os-$arch")
[ -x "$src/bin/$product" ] || fail "the archive does not hold bin/$product"

# Replace the previous installation's files only, leaving anything else in the prefix.
# Each library directory is copied in full
# before the old one is swapped out, so a failed copy leaves the previous installation
# working.
mkdir -p "$prefix/bin" "$prefix/lib/gamma" "$prefix/share/doc/gamma"
for dir in "$src"/lib/gamma/*; do
    name=$(basename "$dir")
    rm -rf "$prefix/lib/gamma/$name.new" "$prefix/lib/gamma/$name.old"
    cp -R "$dir" "$prefix/lib/gamma/$name.new"
    if [ -d "$prefix/lib/gamma/$name" ]; then
        mv "$prefix/lib/gamma/$name" "$prefix/lib/gamma/$name.old"
    fi
    mv "$prefix/lib/gamma/$name.new" "$prefix/lib/gamma/$name"
    rm -rf "$prefix/lib/gamma/$name.old"
done
cp "$src/LICENSE" "$src/README.md" "$prefix/share/doc/gamma/"
cp "$src/bin/$product" "$prefix/bin/$product.new"
mv -f "$prefix/bin/$product.new" "$prefix/bin/$product"

say "Installed $("$prefix/bin/$product" version) in $prefix"
case ":$PATH:" in
    *":$prefix/bin:"*) ;;
    *) say "Add $prefix/bin to your PATH, for example in your shell's profile:
    export PATH=\"$prefix/bin:\$PATH\"" ;;
esac
say "Native builds ($product build, $product run) also need a C compiler: cc, gcc or clang."
