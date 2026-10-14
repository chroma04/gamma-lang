#!/bin/sh
# Installs a prebuilt `gamma` release on Linux or macOS (x86_64 or arm64):
#
#     curl -fsSL https://github.com/chroma04/gamma-lang/releases/latest/download/install.sh | sh
#
# Environment:
#   GAMMA_VERSION   the release to install, such as `v0.1.0` (default: the latest)
#   GAMMA_INSTALL   the installation prefix (default: ~/.gamma); the compiler goes to
#                   $GAMMA_INSTALL/bin/gamma, the standard library to $GAMMA_INSTALL/lib/gamma
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
asset="gamma-$os-$arch.tar.gz"

if [ -n "${GAMMA_DOWNLOAD:-}" ]; then
    base=$GAMMA_DOWNLOAD
elif [ -n "${GAMMA_VERSION:-}" ]; then
    base="$repo/releases/download/$GAMMA_VERSION"
else
    base="$repo/releases/latest/download"
fi

if command -v curl >/dev/null 2>&1; then
    fetch() { curl -fsSL -o "$2" "$1"; }
elif command -v wget >/dev/null 2>&1; then
    fetch() { wget -q -O "$2" "$1"; }
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
actual=$(sha256 "$work/$asset")
[ "$expected" = "$actual" ] || fail "checksum mismatch for $asset (expected $expected, got $actual)"

tar -C "$work" -xzf "$work/$asset"
src=$(echo "$work"/gamma-*-"$os-$arch")
[ -x "$src/bin/gamma" ] || fail "the archive does not hold bin/gamma"

# Replace the previous installation's files only, leaving anything else in the prefix.
mkdir -p "$prefix/bin" "$prefix/lib" "$prefix/share/doc/gamma"
rm -rf "$prefix/lib/gamma"
cp -R "$src/lib/gamma" "$prefix/lib/gamma"
cp "$src/LICENSE" "$src/README.md" "$prefix/share/doc/gamma/"
cp "$src/bin/gamma" "$prefix/bin/gamma.new"
mv -f "$prefix/bin/gamma.new" "$prefix/bin/gamma"

say "Installed $("$prefix/bin/gamma" version) in $prefix"
case ":$PATH:" in
    *":$prefix/bin:"*) ;;
    *) say "Add $prefix/bin to your PATH, for example in your shell's profile:
    export PATH=\"$prefix/bin:\$PATH\"" ;;
esac
say "Native builds (gamma build, gamma run --backend=c) also need a C compiler: cc, gcc or clang."
