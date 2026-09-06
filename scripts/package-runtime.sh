#!/usr/bin/env bash

set -euo pipefail

readonly BUILD_DIR="${BUILD_DIR:-build}"
readonly TARGET="${TARGET:?TARGET is required}"
readonly RUNTIME_REVISION="${RUNTIME_REVISION:?RUNTIME_REVISION is required}"
readonly PHP_CONFIG="${PHP_CONFIG:-php-config}"
readonly PHP_VERSION="$("${PHP_CONFIG}" --version)"
readonly PHP_PREFIX="$("${PHP_CONFIG}" --prefix)"
readonly PHP_LICENSE_DIR="${PHP_LICENSE_DIR:-${PHP_PREFIX}/license}"
readonly PACKAGE_NAME="pox-php-${PHP_VERSION}-${RUNTIME_REVISION}-${TARGET}"
readonly STAGE_DIR="${BUILD_DIR}/${PACKAGE_NAME}"

case "${TARGET}" in
    *-apple-darwin) readonly LIBRARY_NAME="libpox_php.dylib" ;;
    *-unknown-linux-*) readonly LIBRARY_NAME="libpox_php.so" ;;
    *) echo "Unsupported runtime target: ${TARGET}" >&2; exit 1 ;;
esac

rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}/licenses"
cp "${BUILD_DIR}/${LIBRARY_NAME}" "${STAGE_DIR}/${LIBRARY_NAME}"
cp LICENSE "${STAGE_DIR}/licenses/pox-runtime-MIT.txt"

# SPC collects the licenses for the actual selected PHP/extensions/libraries in
# its SDK prefix. The download tool's own .spc directory is not the build tree.
if [[ ! -d "${PHP_LICENSE_DIR}" ]] || [[ -z "$(find "${PHP_LICENSE_DIR}" -type f -print -quit)" ]]; then
    echo "PHP SDK license files are missing: ${PHP_LICENSE_DIR}. Set PHP_LICENSE_DIR to the collected notices before packaging." >&2
    exit 1
fi
mkdir -p "${STAGE_DIR}/licenses/php"
cp -R "${PHP_LICENSE_DIR}/." "${STAGE_DIR}/licenses/php/"

python3 scripts/write-manifest.py \
    --library "${STAGE_DIR}/${LIBRARY_NAME}" \
    --output "${STAGE_DIR}/runtime.json" \
    --php-version "${PHP_VERSION}" \
    --runtime-revision "${RUNTIME_REVISION}" \
    --target "${TARGET}"

tar -cf - -C "${BUILD_DIR}" "${PACKAGE_NAME}" \
    | zstd -T0 -19 --force -o "${BUILD_DIR}/${PACKAGE_NAME}.tar.zst"
if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "${BUILD_DIR}/${PACKAGE_NAME}.tar.zst" > "${BUILD_DIR}/${PACKAGE_NAME}.tar.zst.sha256"
else
    shasum -a 256 "${BUILD_DIR}/${PACKAGE_NAME}.tar.zst" > "${BUILD_DIR}/${PACKAGE_NAME}.tar.zst.sha256"
fi
