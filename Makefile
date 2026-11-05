# Thin wrapper around CMake.  CMakeLists.txt is the single source of truth.
BUILD_DIR ?= build
CONFIG    ?= Release

.PHONY: all build debug release test run clean

all: build

build:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CONFIG)
	cmake --build $(BUILD_DIR) --parallel

debug:
	$(MAKE) build BUILD_DIR=build-debug CONFIG=Debug

release:
	$(MAKE) build BUILD_DIR=build-release CONFIG=Release

test:
	$(MAKE) build BUILD_DIR=build-test CONFIG=Debug
	ctest --test-dir build-test --output-on-failure

run: build
	./$(BUILD_DIR)/bin/RMGBE

clean:
	rm -rf build build-debug build-release build-test
