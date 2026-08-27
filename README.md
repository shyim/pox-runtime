# Pox PHP Runtime

This repository builds the independently versioned PHP runtimes loaded by
[Pox](https://github.com/shyim/pox). Each release contains a ZTS/embed PHP
shared library with its native dependencies and a single stable public symbol:
`pox_php_get_api`.

PHP and Zend headers are private implementation details. Pox communicates with
the runtime through the versioned ABI in
[`include/pox_php_runtime.h`](include/pox_php_runtime.h), using opaque handles,
length-delimited buffers, and explicit ownership.

## Local build

With an embed-enabled ZTS PHP installation:

```bash
make test PHP_CONFIG=/path/to/php-config \
  TARGET=x86_64-unknown-linux-gnu
```

To build PHP and its dependencies through static-php-cli first:

```bash
PHP_VERSION=8.5 \
SPC_LIBC=glibc \
TARGET=x86_64-unknown-linux-gnu \
./scripts/build-php-runtime.sh
```

The runtime currently targets PHP 8.4 and 8.5 on Linux glibc/musl for x86_64
and aarch64. Release archives include runtime metadata and license notices. The
channel index is signed with Ed25519; Pox rejects unsigned or corrupted
downloads. The active verification key is published in
[`keys/runtime-index-ed25519.pub`](keys/runtime-index-ed25519.pub); rotations
add a new trusted key to Pox before changing the release signing secret.
