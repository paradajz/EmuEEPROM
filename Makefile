ROOT_MAKEFILE_DIR := $(realpath $(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
BUILD_DIR_BASE    := $(ROOT_MAKEFILE_DIR)/build
SCRIPTS_DIR       := $(ROOT_MAKEFILE_DIR)/scripts
CLANG_TIDY_OUT    := $(BUILD_DIR_BASE)/clang-tidy-fixes.yaml
CF_FAIL_ON_DIFF   := 0

lib:
	@mkdir -p $(BUILD_DIR_BASE) && \
	cd $(BUILD_DIR_BASE) && \
	cmake .. \
	-DCMAKE_BUILD_TYPE=Debug \
	-DEMU_EEPROM_TESTS=ON \
	&& \
	make

test: lib
	cd $(BUILD_DIR_BASE) && \
	cd tests && \
	ctest

format:
	@echo Checking code formatting...
	@find . -regex '.*\.\(cpp\|hpp\|h\|cc\|cxx\)' \
	-exec clang-format -style=file -i {} \;
ifeq ($(CF_FAIL_ON_DIFF), 1)
	@git diff -s --exit-code
endif

lint: lib
	@cd $(BUILD_DIR_BASE) && \
	run-clang-tidy \
	-style=file \
	-fix \
	-format \
	-export-fixes $(CLANG_TIDY_OUT)
	@if [ -s $(CLANG_TIDY_OUT) ]; then \
		echo Lint issues found:; \
		cat $(CLANG_TIDY_OUT); \
		false; \
	fi

clean:
	@echo Cleaning up.
	@rm -rf $(BUILD_DIR_BASE)

print-%:
	@echo '$*=$($*)'

.PHONY: lib test format lint clean