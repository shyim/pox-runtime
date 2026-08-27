#!/usr/bin/env bash

set -euo pipefail

readonly PHP_CONFIG="${PHP_CONFIG:-php-config}"
readonly BUILD_DIR="${BUILD_DIR:-build}"
readonly RUNTIME_REVISION="${RUNTIME_REVISION:-dev}"
readonly TARGET="${TARGET:-unknown-linux-gnu}"
readonly CC="${CC:-cc}"
readonly PHP_PREFIX="$("${PHP_CONFIG}" --prefix)"
readonly PHP_INCLUDES="$("${PHP_CONFIG}" --includes)"
readonly PHP_LDFLAGS="$("${PHP_CONFIG}" --ldflags)"
readonly PHP_LIBS="$("${PHP_CONFIG}" --libs)"

case "${TARGET}" in
    *-apple-darwin)
        readonly PLATFORM="macos"
        readonly LIBRARY_NAME="libpox_php.dylib"
        ;;
    *-unknown-linux-*)
        readonly PLATFORM="linux"
        readonly LIBRARY_NAME="libpox_php.so"
        ;;
    *)
        echo "Unsupported runtime target: ${TARGET}" >&2
        exit 1
        ;;
esac

read -r -a php_lib_tokens <<< "${PHP_LIBS}"
php_link_tokens=()
cxx_link_tokens=()
if [[ "${PLATFORM}" == "macos" ]]; then
    php_link_tokens=("${php_lib_tokens[@]}")
else
    readonly CXX_STATIC_ARCHIVE="$("${CC}" -print-file-name=libstdc++.a)"
    if [[ -f "${CXX_STATIC_ARCHIVE}" ]]; then
        for token in "${php_lib_tokens[@]}"; do
            if [[ "${token}" != "-lstdc++" ]]; then
                php_link_tokens+=("${token}")
            fi
        done
        cxx_link_tokens+=("${CXX_STATIC_ARCHIVE}")
    elif [[ "${POX_ALLOW_DYNAMIC_CXX:-0}" == "1" ]]; then
        php_link_tokens=("${php_lib_tokens[@]}")
    else
        echo "A static libstdc++.a is required for release runtimes." >&2
        echo "Install the host libstdc++ static package or set POX_ALLOW_DYNAMIC_CXX=1 for local testing." >&2
        exit 1
    fi
fi

mkdir -p "${BUILD_DIR}"

# shellcheck disable=SC2086
"${CC}" -std=gnu11 -O2 -fPIC -fvisibility=hidden -Wall -Wextra \
    -D_GNU_SOURCE \
    -DPOX_RUNTIME_REVISION="\"${RUNTIME_REVISION}\"" \
    -DPOX_RUNTIME_TARGET="\"${TARGET}\"" \
    -Iinclude -I"${PHP_PREFIX}/include" -I"${PHP_PREFIX}/include/libxml2" ${PHP_INCLUDES} \
    -c src/runtime.c -o "${BUILD_DIR}/runtime.o"

# Keep PHP and every static dependency private to the runtime. The explicit
# codec libraries cover dependencies omitted by php-config in SPC builds.
if [[ "${PLATFORM}" == "macos" ]]; then
    # shellcheck disable=SC2086
    "${CC}" -dynamiclib -o "${BUILD_DIR}/${LIBRARY_NAME}" \
        "${BUILD_DIR}/runtime.o" \
        ${PHP_LDFLAGS} \
        -Wl,-force_load,"${PHP_PREFIX}/lib/libphp.a" \
        "${php_link_tokens[@]}" -laom -lsharpyuv \
        -Wl,-exported_symbol,_pox_php_get_api \
        -Wl,-undefined,error \
        -Wl,-install_name,@rpath/libpox_php.dylib

    if [[ "$(nm -gUj "${BUILD_DIR}/${LIBRARY_NAME}")" != "_pox_php_get_api" ]]; then
        echo "Unexpected exported symbol set" >&2
        nm -gU "${BUILD_DIR}/${LIBRARY_NAME}" >&2
        exit 1
    fi
    unexpected_dependencies="$(
        otool -L "${BUILD_DIR}/${LIBRARY_NAME}" \
            | tail -n +2 \
            | awk '{print $1}' \
            | grep -Ev '^(@rpath/libpox_php\.dylib|/usr/lib/|/System/Library/Frameworks/)' || true
    )"
else
    # shellcheck disable=SC2086
    "${CC}" -shared -o "${BUILD_DIR}/${LIBRARY_NAME}" \
        "${BUILD_DIR}/runtime.o" \
        ${PHP_LDFLAGS} \
        -Wl,--whole-archive "${PHP_PREFIX}/lib/libphp.a" -Wl,--no-whole-archive \
        -Wl,--start-group "${php_link_tokens[@]}" "${cxx_link_tokens[@]}" -laom -lsharpyuv -Wl,--end-group \
        -Wl,--exclude-libs,ALL \
        -static-libstdc++ -static-libgcc \
        -Wl,--version-script=src/exports.map \
        -Wl,-z,defs \
        -Wl,-soname,libpox_php.so.1

    if [[ "$(nm -D --defined-only "${BUILD_DIR}/${LIBRARY_NAME}" | awk '{print $3}' | grep -v '^POX_PHP_1.0$')" != "pox_php_get_api@@POX_PHP_1.0" ]]; then
        echo "Unexpected exported symbol set" >&2
        nm -D --defined-only "${BUILD_DIR}/${LIBRARY_NAME}" >&2
        exit 1
    fi

    dependency_allowlist='^(libc(\.so|\.musl)|libm\.so|libresolv\.so|ld-linux|ld-musl)'
    if [[ "${POX_ALLOW_DYNAMIC_CXX:-0}" == "1" ]]; then
        dependency_allowlist='^(libc(\.so|\.musl)|libm\.so|libresolv\.so|libstdc\+\+\.so|libgcc_s\.so|ld-linux|ld-musl)'
    fi
    unexpected_dependencies="$(
        readelf -d "${BUILD_DIR}/${LIBRARY_NAME}" \
            | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p' \
            | grep -Ev "${dependency_allowlist}" || true
    )"
fi

if [[ -n "${unexpected_dependencies}" ]]; then
    echo "Runtime has unexpected dynamic dependencies:" >&2
    echo "${unexpected_dependencies}" >&2
    exit 1
fi
