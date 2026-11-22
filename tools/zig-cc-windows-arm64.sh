#!/bin/sh
# A C compiler for ARM64 Windows (aarch64-w64-mingw32) that runs on Linux, for building
# gamma.exe for that target:
#
#   pip install ziglang==0.17.1
#   CC=tools/zig-cc-windows-arm64.sh gamma build compiler --target=aarch64-w64-mingw32 -o gamma.exe
#
# It is Zig's `zig cc` (Clang with MinGW-w64 headers and libraries), plus the import library
# of the synchronization API set (WaitOnAddress), which Zig's MinGW-w64 lacks and the
# runtime links. That library is made once, in GAMMA_ZIG_LIB_DIR (default: a directory in
# TMPDIR). PYTHON names the Python that has the ziglang package (default: python3).
set -eu

python="${PYTHON:-python3}"
dir="${GAMMA_ZIG_LIB_DIR:-${TMPDIR:-/tmp}/gamma-zig-windows-arm64}"
if [ ! -f "$dir/libsynchronization.a" ]; then
    mkdir -p "$dir"
    tmp="$(mktemp -d "$dir/lib.XXXXXX")"
    printf 'LIBRARY api-ms-win-core-synch-l1-2-0.dll\nEXPORTS\nWaitOnAddress\nWakeByAddressAll\nWakeByAddressSingle\n' > "$tmp/synchronization.def"
    "$python" -m ziglang dlltool -m arm64 -d "$tmp/synchronization.def" -l "$tmp/libsynchronization.a"
    # Several compilations may start at once: the first to finish puts the library in place.
    mv -f "$tmp/libsynchronization.a" "$dir/libsynchronization.a"
    rm -rf "$tmp"
fi
exec "$python" -m ziglang cc -target aarch64-windows-gnu -L"$dir" "$@"
