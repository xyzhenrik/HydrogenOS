.PHONY: configure build run-shell run-settings test lint check clean

BUILD_DIR ?= build

configure:
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Debug

build: configure
	cmake --build $(BUILD_DIR)

run-shell: build
	$(BUILD_DIR)/shell/hydrogen-shell

run-settings: build
	$(BUILD_DIR)/settings/hydrogen-settings

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure
	@if command -v cargo >/dev/null 2>&1; then cargo test --workspace --all-targets; else echo "cargo not installed; Rust tests skipped locally"; fi

lint:
	@if command -v cargo >/dev/null 2>&1; then cargo fmt --all --check && cargo clippy --workspace --all-targets -- -D warnings; else echo "cargo not installed; Rust lint skipped locally"; fi

check: lint test

clean:
	cmake -E remove_directory $(BUILD_DIR)
	cargo clean 2>/dev/null || true

