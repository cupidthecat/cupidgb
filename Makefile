BUILD_DIR ?= build
CMAKE ?= cmake

.PHONY: configure build run test clean debug release

configure:
	$(CMAKE) -S . -B $(BUILD_DIR)

build: configure
	$(CMAKE) --build $(BUILD_DIR)

run: build
	./$(BUILD_DIR)/cupidgb

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	rm -rf $(BUILD_DIR)

debug:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	$(CMAKE) --build $(BUILD_DIR)

release:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD_DIR)
