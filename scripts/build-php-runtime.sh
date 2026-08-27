#!/usr/bin/env bash

set -euo pipefail

readonly PHP_VERSION="${PHP_VERSION:?PHP_VERSION is required}"
readonly RUNTIME_REVISION="${RUNTIME_REVISION:-r1}"
readonly TARGET="${TARGET:?TARGET is required}"
readonly SPC_LIBC="${SPC_LIBC:?SPC_LIBC is required}"
readonly BUILD_DIR="${BUILD_DIR:-build}"
readonly SPC_DIR="${SPC_DIR:-.spc}"
readonly SPC_BIN="${SPC_DIR}/spc"
readonly PHP_EXTENSIONS="bz2,redis,bcmath,calendar,ctype,curl,dom,exif,fileinfo,filter,gd,iconv,intl,mbregex,mbstring,mysqli,mysqlnd,opcache,openssl,pcntl,pdo,pdo_mysql,pdo_sqlite,phar,posix,session,simplexml,sockets,sodium,sqlite3,tokenizer,xml,xmlreader,xmlwriter,zip,zlib,zstd"
readonly PHP_BUILD_LIBS="libavif,libwebp,libjpeg,freetype,nghttp2,brotli"

mkdir -p "${SPC_DIR}" "${BUILD_DIR}"
if [[ ! -x "${SPC_BIN}" ]]; then
    curl --fail --location \
        --output "${SPC_BIN}" \
        "https://dl.static-php.dev/static-php-cli/spc-bin/nightly/spc-linux-$(uname -m)"
    chmod 0755 "${SPC_BIN}"
fi

export SPC_DEFAULT_C_FLAGS="-fPIC -O3"
export SPC_REL_TYPE="binary"

"${SPC_BIN}" doctor --auto-fix --no-interaction
"${SPC_BIN}" download \
    --with-php="${PHP_VERSION}" \
    --for-extensions="${PHP_EXTENSIONS}" \
    --for-libs="${PHP_BUILD_LIBS}" \
    --prefer-pre-built \
    --retry=3 \
    --no-interaction
"${SPC_BIN}" build "${PHP_EXTENSIONS}" \
    --build-embed \
    --enable-zts \
    --with-libs="${PHP_BUILD_LIBS}" \
    --no-interaction

env PHP_CONFIG="${PWD}/buildroot/bin/php-config" \
    BUILD_DIR="${BUILD_DIR}" \
    RUNTIME_REVISION="${RUNTIME_REVISION}" \
    TARGET="${TARGET}" \
    ./scripts/build-runtime.sh
env PHP_CONFIG="${PWD}/buildroot/bin/php-config" \
    BUILD_DIR="${BUILD_DIR}" \
    RUNTIME_REVISION="${RUNTIME_REVISION}" \
    TARGET="${TARGET}" \
    ./scripts/package-runtime.sh
