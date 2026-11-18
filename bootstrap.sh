#!/bin/sh
# Fetch the pinned host compiler before cross-compiling the firmware.
set -eu
cd "$(dirname "$0")"
ref=v0.4.4
checkout=subprojects/mlang
if [ ! -d "$checkout" ]; then
    mkdir -p subprojects
    git clone --depth 1 --branch "$ref" https://github.com/mattilaa/mlang.git "$checkout"
fi
if [ "$(git -C "$checkout" describe --tags --exact-match 2>/dev/null || true)" != "$ref" ]; then
    echo "bootstrap.sh: $checkout must be checked out at $ref; preserving the existing checkout" >&2
    exit 1
fi
if [ ! -f "$checkout/build/CMakeCache.txt" ]; then
    cmake -S "$checkout" -B "$checkout/build" -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$checkout/build" --target mlang --parallel "${JOBS:-4}"
