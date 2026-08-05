SHELL := /bin/bash

# Disable built-in rules and variables
MAKEFLAGS += --no-builtin-rules
MAKEFLAGS += --no-builtin-variables

NETWORK := local

###########################################################################
# OS we're running on
ifeq ($(OS),Windows_NT)
	detected_OS := Windows
else
	detected_OS := $(shell sh -c 'uname 2>/dev/null || echo Unknown')
endif

ifeq ($(detected_OS),Darwin)	  # Mac OS X  (Intel)
	OS += macos
	DIDC += didc-macos
endif
ifeq ($(detected_OS),Linux)		  # Ubuntu
	OS += linux
	DIDC += didc-linux64 
endif

ifeq ($(detected_OS),Windows_NT)  # Windows (icpp supports it but you cannot run this Makefile)
	OS += windows_cannot_run_make
endif
ifeq ($(detected_OS),Unknown)     # Unknown
	OS += unknown
endif

###########################################################################
# latest release of didc
VERSION_DIDC := $(shell curl --silent "https://api.github.com/repos/dfinity/candid/releases/latest" | grep -e '"tag_name"' | cut -c 16-25)
# version to install for clang
VERSION_CLANG := $(shell cat version_clang.txt)
# version to install for icp-cli
VERSION_ICP := 1.2.0

###########################################################################
# Use some clang tools that come with wasi-sdk
ICPP_COMPILER_ROOT := $(HOME)/.icpp/wasi-sdk/wasi-sdk-25.0
CLANG_FORMAT = $(ICPP_COMPILER_ROOT)/bin/clang-format
CLANG_TIDY = $(ICPP_COMPILER_ROOT)/bin/clang-tidy


###########################################################################
# The icp identities used by the wasm QA (deploy + pytest).
#
# icpp-pro >= 6.0.0 never reads or writes the machine-wide active identity
# (`icp identity default`): pytest is told which identity to run as, via
# ${ICPP_PRO_TEST_IDENTITY}. A canister's controller is whoever deployed it, so
# scripts/qa_deploy_and_pytest.py deploys as that same identity.
#
# ICPP_PRO_TEST_IDENTITY_NON_CONTROLLER is a second identity that is never a
# controller. It is what the `*_non_controller` tests in test/test_files.py call
# as, to prove the endpoints deny a caller who is neither a controller nor an
# admin.
ICPP_PRO_TEST_IDENTITY ?= llama-cpp-testing
ICPP_PRO_TEST_IDENTITY_NON_CONTROLLER ?= llama-cpp-other-user
export ICPP_PRO_TEST_IDENTITY
export ICPP_PRO_TEST_IDENTITY_NON_CONTROLLER

###########################################################################
# CI/CD - Phony Makefile targets
#
.PHONY: all-tests
all-tests: all-static test-llm-wasm test-llm-native 

.PHONY: build-info-cpp-wasm
build-info-cpp-wasm:
	@echo "--"
	@echo "Creating src/llama_cpp_onicai_fork/common/build-info.cpp for build-wasm"
	sh scripts/build-info-cpp.sh $(ICPP_COMPILER_ROOT)/bin/clang > src/llama_cpp_onicai_fork/common/build-info.cpp
	@echo "Content of src/llama_cpp_onicai_fork/common/build-info.cpp:"
	@echo " " 
	@cat src/llama_cpp_onicai_fork/common/build-info.cpp 

.PHONY: build-info-cpp-native
build-info-cpp-native:
	@echo "--"
	@echo "Creating src/llama_cpp_onicai_fork/common/build-info.cpp for build-native"
	sh scripts/build-info-cpp.sh clang > src/llama_cpp_onicai_fork/common/build-info.cpp
	@echo "Content of src/llama_cpp_onicai_fork/common/build-info.cpp:"
	@echo " " 
	@cat src/llama_cpp_onicai_fork/common/build-info.cpp

.PHONY: summary
summary:
	@echo "-------------------------------------------------------------"
	@echo OS=$(OS)
	@echo VERSION_DIDC=$(VERSION_DIDC)
	@echo VERSION_CLANG=$(VERSION_CLANG)
	@echo CLANG_FORMAT=$(CLANG_FORMAT)
	@echo CLANG_TIDY=$(CLANG_TIDY)
	@echo ICPP_COMPILER_ROOT=$(ICPP_COMPILER_ROOT)
	@echo "-------------------------------------------------------------"

.PHONY: test-llm-native
test-llm-native:
	icpp build-native
	./build-native/mockic.exe

.PHONY: test-llm-wasm
test-llm-wasm: icp-test-identities
	python -m scripts.qa_deploy_and_pytest

# Creates the two QA identities if they do not exist yet, without ever touching
# the machine-wide active identity.
#
# The existence check is `icp identity principal --identity <name>`, not
# `icp identity list | grep -w <name>`: grep treats `-` as a word boundary, so
# `<name>-old` would match `<name>` and the identity would silently not be
# created. They are stored in plaintext because icpp-pro exports the key to sign
# locally, which a password-protected identity cannot do non-interactively.
.PHONY: icp-test-identities
icp-test-identities:
	@icp identity principal --identity "$(ICPP_PRO_TEST_IDENTITY)" >/dev/null 2>&1 || \
	  icp identity new "$(ICPP_PRO_TEST_IDENTITY)" --storage plaintext
	@icp identity principal --identity "$(ICPP_PRO_TEST_IDENTITY_NON_CONTROLLER)" >/dev/null 2>&1 || \
	  icp identity new "$(ICPP_PRO_TEST_IDENTITY_NON_CONTROLLER)" --storage plaintext

.PHONY: all-static
all-static: \
	cpp-format cpp-lint \
	python-format python-lint python-type
	
CPP_AND_H_FILES = $(shell ls \
	src/*.cpp src/*.h \
	native/*.cpp native/*.h \
	| grep -v "src/main_\.cpp")


.PHONY: cpp-format
cpp-format:
	@echo "---"
	@echo "cpp-format"
	$(CLANG_FORMAT) --style=file --verbose -i $(CPP_AND_H_FILES)

.PHONY: cpp-lint
cpp-lint:
	@echo "---"
	@echo "cpp-lint"
	@echo "TO IMPLEMENT with clang-tidy"

PYTHON_DIRS ?= scripts

.PHONY: python-format
python-format:
	@echo "---"
	@echo "python-format"
	python -m black $(PYTHON_DIRS)

.PHONY: python-lint
python-lint:
	@echo "---"
	@echo "python-lint"
	python -m pylint --jobs=0 --rcfile=.pylintrc $(PYTHON_DIRS)

.PHONY: python-type
python-type:
	@echo "---"
	@echo "python-type"
	python -m mypy --config-file .mypy.ini --show-column-numbers --strict --explicit-package-bases $(PYTHON_DIRS)


###########################################################################
# Toolchain installation for .github/workflows

.PHONY: install-clang-ubuntu
install-clang-ubuntu:
	@echo "Installing clang-$(VERSION_CLANG) compiler"
	# sudo apt-get remove python3-lldb-14
	wget https://apt.llvm.org/llvm.sh
	chmod +x llvm.sh
	echo | sudo ./llvm.sh $(VERSION_CLANG)
	rm llvm.sh

	@echo "Creating soft links for compiler executables"
	sudo ln --force -s /usr/bin/clang-$(VERSION_CLANG) /usr/bin/clang
	sudo ln --force -s /usr/bin/clang++-$(VERSION_CLANG) /usr/bin/clang++

# This installs the icp-cli (and ic-wasm) globally via npm. Requires Node.js >= 22.
.PHONY: install-icp
install-icp:
	npm install -g @icp-sdk/icp-cli@$(VERSION_ICP) @icp-sdk/ic-wasm

.PHONY: install-didc
install-didc:
	@echo "Installing didc $(VERSION_DIDC) ..."
	sudo rm -rf /usr/local/bin/didc
	wget https://github.com/dfinity/candid/releases/download/${VERSION_DIDC}/$(DIDC)
	sudo mv $(DIDC) /usr/local/bin/didc
	chmod +x /usr/local/bin/didc
	@echo " "
	@echo "Installed successfully in:"
	@echo /usr/local/bin/didc

.PHONY: install-jp-ubuntu
install-jp-ubuntu:
	sudo apt-get update && sudo apt-get install jp

.PHONY: install-jp-mac
install-jp-mac:
	brew install jp

.PHONY: install-homebrew-mac
install-homebrew-mac:
	/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

.PHONY: install-python
install-python:
	pip install --upgrade pip
	pip install -r requirements.txt