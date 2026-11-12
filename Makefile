BUILD := build
# PREFIX ?= $(HOME)/.local
PREFIX ?= /usr/local

BUILD_TYPE ?= Release
CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build:
	cmake -S . -B $(BUILD) -G Ninja $(CMAKE_FLAGS)
	cmake --build $(BUILD)

run: build
	@set -a; [ ! -f .env ] || . ./.env; set +a; ./$(BUILD)/dkbom

test: build
	ctest --test-dir $(BUILD) --output-on-failure

install:
	cmake --install $(BUILD) --prefix $(PREFIX)

uninstall:
	cmake --build $(BUILD) --target uninstall

clean:
	rm -rf $(BUILD)

.PHONY: build run test install uninstall clean
