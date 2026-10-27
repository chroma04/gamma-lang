#!/bin/sh
# Checks that the Gamma site generator in web/gen produces exactly what the Rust one in
# web/sitegen does, on this repository:
#
#   1. both build the site, and the two output trees must be identical;
#   2. both check the links of a copy of the repository with a deliberately broken
#      anchor, and must fail with the same messages.
#
# The Rust generator needs cargo. The Gamma one is built with gammac, found as $GAMMAC
# (default: gammac on PATH), which needs GAMMA_STD and GAMMA_RUNTIME to point at lib/std
# and runtime/ of a checkout unless it is installed.
#
#   web/gen/check-parity.sh

set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
gammac=${GAMMAC:-gammac}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "building the Rust generator"
cargo build --release --quiet --manifest-path "$root/web/sitegen/Cargo.toml"
rust_gen=$root/web/sitegen/target/release/gamma-sitegen

echo "building the Gamma generator"
"$gammac" build "$root/web/gen" -o "$tmp/sitegen"

echo "building the site with both"
"$rust_gen" --root "$root" --out "$tmp/rust"
"$tmp/sitegen" --root "$root" --out "$tmp/gamma"
diff -r "$tmp/rust" "$tmp/gamma"
echo "the sites are identical"

echo "checking a site with a broken anchor"
mkdir "$tmp/broken"
for f in web docs examples gamma-spec.md CHANGELOG.md SECURITY.md; do
    cp -R "$root/$f" "$tmp/broken/$f"
done
printf '\nSee [nowhere](#no-such-anchor).\n' >> "$tmp/broken/docs/lint.md"
status_rust=0
status_gamma=0
"$rust_gen" --root "$tmp/broken" --out "$tmp/broken-rust" > "$tmp/rust.out" 2> "$tmp/rust.err" || status_rust=$?
"$tmp/sitegen" --root "$tmp/broken" --out "$tmp/broken-gamma" > "$tmp/gamma.out" 2> "$tmp/gamma.err" || status_gamma=$?
if [ "$status_rust" -eq 0 ] || [ "$status_gamma" -eq 0 ]; then
    echo "the link check should have failed (Rust: $status_rust, Gamma: $status_gamma)" >&2
    exit 1
fi
grep -q 'missing anchor in link #no-such-anchor' "$tmp/gamma.err"
sed "s|$tmp/broken-rust|OUT|" "$tmp/rust.out" > "$tmp/rust.out.norm"
sed "s|$tmp/broken-gamma|OUT|" "$tmp/gamma.out" > "$tmp/gamma.out.norm"
diff "$tmp/rust.out.norm" "$tmp/gamma.out.norm"
diff "$tmp/rust.err" "$tmp/gamma.err"
echo "both reject the broken anchor with the same messages"
