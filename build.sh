#!/bin/sh
set -eu
cd "$(dirname "$0")"
./bootstrap.sh
cmake -S . -B "${BUILD_DIR:-build}" -DMLANG_ROOT="${MLANG_ROOT:-$PWD/subprojects/mlang}" "$@"
cmake --build "${BUILD_DIR:-build}" --parallel "${JOBS:-4}"
