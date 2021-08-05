MAKEFLAGS += --warn-undefined-variables

.PHONY: build check-format format release Master Slave Testing TestESP32 flash-Master flash-Slave

MOS ?= mos
# Build locally by default if Docker is available.
LOCAL ?= $(shell which docker> /dev/null && echo 1 || echo 0)
CLEAN ?= 0
V ?= 0
VERBOSE ?= 0
RELEASE ?= 0
RELEASE_SUFFIX ?=
MOS_BUILD_FLAGS ?=
BUILD_DIR ?= ./build_$*

MOS_BUILD_FLAGS_FINAL = $(MOS_BUILD_FLAGS)
ifeq "$(LOCAL)" "1"
  MOS_BUILD_FLAGS_FINAL += --local
endif
ifeq "$(CLEAN)" "1"
  MOS_BUILD_FLAGS_FINAL += --clean
endif
ifneq "$(VERBOSE)$(V)" "00"
  MOS_BUILD_FLAGS_FINAL += --verbose
endif

build: Master Slave

release:
	@[ -z "$(wildcard fs/conf*.json)" ] || { echo; echo "XXX No configs in release builds allowed"; echo; exit 1; }
	$(MAKE) build CLEAN=1 RELEASE=1

PLATFORM ?= esp8266

Master: build-Master
	@true

Slave: build-Slave
	@true

Testing: build-Testing
	@true

TestESP32: PLATFORM=esp32
TestESP32: build-TestESP32
	@true

build-%: Makefile
	$(MOS) build --platform=$(PLATFORM) --build-var=MODEL=$* \
	  --build-dir=$(BUILD_DIR) --binary-libs-dir=./binlibs $(MOS_BUILD_FLAGS_FINAL)

flash-%: $*
	$(MOS) flash --firmware=$(BUILD_DIR)/fw.zip

format:
	for d in src lib*; do find $$d -name \*.cpp -o -name \*.hpp | xargs clang-format -i; done

check-format: format
	@git diff --exit-code || { printf "\n== Please format your code (run make format) ==\n\n"; exit 1; }
