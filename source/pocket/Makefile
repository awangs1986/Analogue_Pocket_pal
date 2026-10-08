# SPDX-License-Identifier: GPL-3.0-or-later
# Isolated target: never defines __MISTER__ and never links Linux HPS code.
.DEFAULT_GOAL := all
SDK_ROOT ?= ../../openfpgaSDK
SDK_DIR := $(abspath $(SDK_ROOT))/src/sdk
ROOT := ..
ENGINE := $(ROOT)/sdlpal
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
USE_SDK_CONTAINER ?= 0
CFLAGS += -std=gnu99 -MMD -MP -D_GNU_SOURCE -DUSE_SDL3=0 -D__POCKET__=1 \
 -DSDL_LOADSO_DISABLED=1 -DSDL_DYNAMIC_API=0 \
 -I. -I$(ENGINE) -I$(ENGINE)/sdl_compat -I$(ENGINE)/liboggvorbis/include \
 -I$(ENGINE)/liboggvorbis/src -Wno-unused-parameter -Wno-sign-compare
LDFLAGS += -Wl,-Map,$(BUILD_DIR)/pal.map \
 -Wl,--wrap=fopen,--wrap=access,--wrap=SDL_Delay,--wrap=PAL_SaveGame
ENGINE_NAMES := aviplay battle ending fight font game global itemmenu magicmenu \
 main map overlay palcfg palcommon paldebug palette play res resampler rngplay \
 scene script sound text ui uibattle uigame util yj1
SRCS := $(addprefix $(ENGINE)/,$(addsuffix .c,$(ENGINE_NAMES))) \
 $(ENGINE)/sdl_compat/sdl_compat.c \
 platform.c video.c input.c files.c audio.c audio_stream.c \
 $(wildcard $(ENGINE)/liboggvorbis/src/*.c)
include $(SDK_DIR)/sdk.mk
-include $(shell find $(BUILD_DIR) -name "*.d" 2>/dev/null)

.PHONY: all clean check pins package
all: $(BUILD_DIR)/app.elf
pins:
	python3 tools/check_sdk.py --sdk $(SDK_ROOT)
check:
	SDK_ROOT=$(abspath $(SDK_ROOT)) python3 -m unittest discover -s tests -p 'test_*.py' -v
	python3 tests/audio_test.py
package: all pins
	python3 tools/package.py --sdk $(SDK_ROOT) --elf $(BUILD_DIR)/app.elf --out $(BUILD_DIR)/package
clean: sdk-clean

.PHONY: probe
probe:
	$(MAKE) BUILD_DIR=build/probe OBJ_DIR=build/probe/obj \
	 SRCS='probe.c files.c audio_stream.c $(wildcard $(ENGINE)/liboggvorbis/src/*.c)' \
	 LDFLAGS='-Wl,-Map,build/probe/pal-probe.map -Wl,--wrap=fopen,--wrap=access' all
