#!/bin/sh
# Build the host compiler before cross-compiling the firmware.
set -eu
cd "$(dirname "$0")"
checkout=${MLANG_ROOT:-subprojects/mlang}
release=v0.4.5
if [ -z "${MLANG_ROOT:-}" ]; then
    if [ ! -d "$checkout" ]; then
        mkdir -p subprojects
        git clone --depth 1 --branch "$release" https://github.com/mattilaa/mlang.git "$checkout"
    fi
    if [ -n "$(git -C "$checkout" status --porcelain)" ]; then
        echo "bootstrap.sh: preserving modified $checkout; commit or stash its changes first" >&2
        exit 1
    fi
    branch=$(git -C "$checkout" branch --show-current)
    tag=$(git -C "$checkout" describe --tags --exact-match 2>/dev/null || true)
    if [ "$branch" != main ] && [ "$tag" != v0.4.4 ] && [ "$tag" != "$release" ]; then
        echo "bootstrap.sh: preserving $checkout on $branch; expected $release (or an old main/v0.4.4 checkout)" >&2
        exit 1
    fi
    if [ "$tag" != "$release" ]; then
        git -C "$checkout" fetch --depth 1 origin "refs/tags/$release:refs/tags/$release"
    fi
    if [ -n "$branch" ] || [ "$tag" != "$release" ]; then
        git -C "$checkout" switch --detach "refs/tags/$release"
    fi
fi
if [ ! -d "$checkout" ]; then
    echo "bootstrap.sh: MLANG_ROOT does not name an existing compiler checkout: $checkout" >&2
    exit 1
fi
set -- -DMLANG_AUTO_INCREMENT_BUILD=OFF
if [ -z "${MLANG_ROOT:-}" ]; then
    set -- "$@" "-DMLANG_RELEASE_VERSION=${release#v}"
fi
if [ ! -f "$checkout/build/CMakeCache.txt" ]; then
    cmake -S "$checkout" -B "$checkout/build" -DBUILD_TESTS=OFF \
        "$@" -DCMAKE_BUILD_TYPE=Release
else
    cmake -S "$checkout" -B "$checkout/build" "$@"
fi
cmake --build "$checkout/build" --target mlang --parallel "${JOBS:-4}"
