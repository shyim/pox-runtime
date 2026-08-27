#!/usr/bin/env bash

set -euo pipefail

readonly INDEX_PATH="${1:?index path is required}"
readonly OUTPUT_PATH="${2:?signature output path is required}"
readonly KEY_ID="${RUNTIME_INDEX_KEY_ID:-pox-runtime-2026-01}"
readonly KEY_FILE="$(mktemp)"
trap 'rm -f "${KEY_FILE}"' EXIT

if [[ -n "${RUNTIME_INDEX_SIGNING_KEY_FILE:-}" ]]; then
    cp "${RUNTIME_INDEX_SIGNING_KEY_FILE}" "${KEY_FILE}"
else
    printf '%s' "${RUNTIME_INDEX_SIGNING_KEY:?RUNTIME_INDEX_SIGNING_KEY is required}" \
        | base64 --decode > "${KEY_FILE}"
fi
chmod 0600 "${KEY_FILE}"

printf '%s:' "${KEY_ID}" > "${OUTPUT_PATH}"
openssl pkeyutl -sign -rawin -inkey "${KEY_FILE}" -in "${INDEX_PATH}" \
    | base64 --wrap=0 >> "${OUTPUT_PATH}"
printf '\n' >> "${OUTPUT_PATH}"
