#!/usr/bin/env bash

set -euo pipefail

readonly BUILD_DIR="${BUILD_DIR:-build}"
readonly TARGET="${TARGET:?TARGET is required}"
readonly RUNTIME_REVISION="${RUNTIME_REVISION:?RUNTIME_REVISION is required}"
readonly PHP_CONFIG="${PHP_CONFIG:-php-config}"
readonly PHP_VERSION="$("${PHP_CONFIG}" --version)"
readonly PACKAGE_NAME="pox-php-${PHP_VERSION}-${RUNTIME_REVISION}-${TARGET}"
readonly STAGE_DIR="${BUILD_DIR}/${PACKAGE_NAME}"

rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}/licenses"
cp "${BUILD_DIR}/libpox_php.so" "${STAGE_DIR}/libpox_php.so"
cp LICENSE "${STAGE_DIR}/licenses/pox-runtime-MIT.txt"

if [[ -d "${SPC_DIR:-.spc}/source" ]]; then
    while IFS= read -r license; do
        license_name="${license#${SPC_DIR:-.spc}/source/}"
        license_name="${license_name//\//-}"
        cp "${license}" "${STAGE_DIR}/licenses/${license_name}"
    done < <(find "${SPC_DIR:-.spc}/source" -maxdepth 3 -type f \
        \( -iname 'license' -o -iname 'license.*' -o -iname 'copying' \) | sort)
fi

python3 scripts/write-manifest.py \
    --library "${STAGE_DIR}/libpox_php.so" \
    --output "${STAGE_DIR}/runtime.json" \
    --php-version "${PHP_VERSION}" \
    --runtime-revision "${RUNTIME_REVISION}" \
    --target "${TARGET}"

tar --zstd -cf "${BUILD_DIR}/${PACKAGE_NAME}.tar.zst" -C "${BUILD_DIR}" "${PACKAGE_NAME}"
sha256sum "${BUILD_DIR}/${PACKAGE_NAME}.tar.zst" > "${BUILD_DIR}/${PACKAGE_NAME}.tar.zst.sha256"
