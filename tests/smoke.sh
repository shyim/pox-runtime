#!/usr/bin/env bash

set -euo pipefail

readonly BUILD_DIR="${BUILD_DIR:-build}"
readonly CC="${CC:-cc}"

if [[ "$(uname -s)" == "Darwin" ]]; then
    readonly LIBRARY_NAME="libpox_php.dylib"
    "${CC}" -std=c11 -Iinclude tests/smoke.c -o "${BUILD_DIR}/smoke"
else
    readonly LIBRARY_NAME="libpox_php.so"
    "${CC}" -std=c11 -Iinclude tests/smoke.c -ldl -o "${BUILD_DIR}/smoke"
fi
"${BUILD_DIR}/smoke" "${BUILD_DIR}/${LIBRARY_NAME}"
