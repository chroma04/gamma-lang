#!/bin/sh
# Fills fuzz/corpus/TARGET for every target: what fuzz/seed.sh makes for the cargo-fuzz
# targets, and a few package archives for the archive target (a valid one and the kinds of
# archive the unpacker must refuse).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
"$root/fuzz/seed.sh"

dir="$root/fuzz/corpus/archive"
mkdir -p "$dir"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/pkg/src"
printf 'name = "x"\n' > "$work/pkg/gamma.toml"
printf 'main :: fn() {}\n' > "$work/pkg/src/main.gm"
(cd "$work" && tar -czf "$dir/plain.tar.gz" pkg)
(cd "$work/pkg" && tar -czf "$dir/flat.tar.gz" gamma.toml src)
ln -s ../../outside "$work/pkg/src/link"
(cd "$work" && tar -czf "$dir/symlink.tar.gz" pkg)
rm "$work/pkg/src/link"
ln "$work/pkg/gamma.toml" "$work/pkg/src/hard"
(cd "$work" && tar -czf "$dir/hardlink.tar.gz" pkg)
rm "$work/pkg/src/hard"
(cd "$work/pkg" && tar -czf "$dir/parent.tar.gz" --transform 's,^,../,' gamma.toml)
(cd "$work/pkg" && tar -czPf "$dir/absolute.tar.gz" --transform 's,^,/tmp/gamma-fuzz-absolute/,' gamma.toml)
mkfifo "$work/pkg/pipe"
(cd "$work" && tar -czf "$dir/fifo.tar.gz" pkg)
