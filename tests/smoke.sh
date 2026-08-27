#!/usr/bin/env bash

set -euo pipefail

readonly BUILD_DIR="${BUILD_DIR:-build}"
readonly CC="${CC:-cc}"

"${CC}" -std=gnu11 -D_GNU_SOURCE -Iinclude tests/smoke.c -ldl -o "${BUILD_DIR}/smoke"
"${BUILD_DIR}/smoke" "${BUILD_DIR}/libpox_php.so"
