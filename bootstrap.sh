#!/bin/sh
# Build the host compiler before cross-compiling the firmware.
set -eu
cd "$(dirname "$0")"
checkout=${MLANG_ROOT:-subprojects/mlang}
if [ -z "${MLANG_ROOT:-}" ]; then
    if [ ! -d "$checkout" ]; then
        mkdir -p subprojects
        git clone --depth 1 --branch main https://github.com/mattilaa/mlang.git "$checkout"
    fi
    if [ -n "$(git -C "$checkout" status --porcelain)" ]; then
        echo "bootstrap.sh: preserving modified $checkout; commit or stash its changes first" >&2
        exit 1
    fi
    branch=$(git -C "$checkout" branch --show-current)
    tag=$(git -C "$checkout" describe --tags --exact-match 2>/dev/null || true)
    if [ "$branch" != main ] && [ "$tag" != v0.4.4 ]; then
        echo "bootstrap.sh: preserving $checkout on $branch; expected main (or the old v0.4.4 checkout)" >&2
        exit 1
    fi
    git -C "$checkout" config remote.origin.fetch "+refs/heads/main:refs/remotes/origin/main"
    git -C "$checkout" fetch origin main
    if [ "$branch" != main ]; then
        git -C "$checkout" switch -c main --track origin/main
    fi
    git -C "$checkout" merge --ff-only origin/main
fi
if [ ! -d "$checkout" ]; then
    echo "bootstrap.sh: MLANG_ROOT does not name an existing compiler checkout: $checkout" >&2
    exit 1
fi
if [ ! -f "$checkout/build/CMakeCache.txt" ]; then
    cmake -S "$checkout" -B "$checkout/build" -DBUILD_TESTS=OFF \
        -DMLANG_AUTO_INCREMENT_BUILD=OFF -DCMAKE_BUILD_TYPE=Release
else
    cmake -S "$checkout" -B "$checkout/build" -DMLANG_AUTO_INCREMENT_BUILD=OFF
fi
cmake --build "$checkout/build" --target mlang --parallel "${JOBS:-4}"
