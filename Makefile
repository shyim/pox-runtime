PHP_CONFIG ?= php-config
BUILD_DIR ?= build
RUNTIME_REVISION ?= dev
TARGET ?= unknown-linux-gnu
CC ?= cc

.PHONY: all clean test package

all:
	PHP_CONFIG="$(PHP_CONFIG)" BUILD_DIR="$(BUILD_DIR)" RUNTIME_REVISION="$(RUNTIME_REVISION)" TARGET="$(TARGET)" ./scripts/build-runtime.sh

test: all
	BUILD_DIR="$(BUILD_DIR)" ./tests/smoke.sh

package: test
	BUILD_DIR="$(BUILD_DIR)" TARGET="$(TARGET)" RUNTIME_REVISION="$(RUNTIME_REVISION)" ./scripts/package-runtime.sh

clean:
	rm -rf "$(BUILD_DIR)"
