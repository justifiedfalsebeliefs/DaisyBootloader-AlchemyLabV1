# Project Name
PROJECT_NAME = dsy_bootloader

VERSION_MAJ=6
VERSION_MIN=4

# Modification flag for using external USB port as DFU
#C_DEFS += -DDSY_DFU_USE_EXT_USB=1
# Modify default boot timeout
#C_DEFS += -DDSY_BOOT_TIMEOUT_MS=200

## THIS MIGHT WORK ON MAC.. BUT ITS GIVING ME A REAL HARD TIME ON WINDOWS :(
## Instead we have a `build_release.sh` script that will do this for
## us in the root of the workspace.

# .PHONY: all
# all:
# 	@echo "Building"

# release:
#   @mkdir -p dist/
# 	@echo "Building internal DFU 2000ms..."
# 	@$(MAKE) clean
# 	@$(MAKE) all EXTRA_C_DEFS="" TARGET_SUFFIX="-intdfu-2000ms"
#   @$(CP) "./build/$(TARGET).bin" dist/
#   @echo "Building internal DFU 10ms..."
#   @$(MAKE) clean
#   @$(MAKE) all EXTRA_C_DEFS="-DDSY_BOOT_TIMEOUT_MS=10" TARGET_SUFFIX="-intdfu-10ms"
#   @$(CP) "build/$(TARGET).bin" dist/
#   @echo "Building external DFU 2000ms..."
#   @$(MAKE) clean
#   @$(MAKE) all EXTRA_C_DEFS="-DDSY_BOOT_TIMEOUT_MS=2000 -DDSY_DFU_USE_EXT_USB" TARGET_SUFFIX="-extdfu-2000ms"
#   @$(CP) "build/$(TARGET).bin" dist/
#   @echo "Building external DFU 10ms..."
#   @$(MAKE) clean
#   @$(MAKE) all EXTRA_C_DEFS="-DDSY_BOOT_TIMEOUT_MS=10 -DDSY_DFU_USE_EXT_USB" TARGET_SUFFIX="-extdfu-10ms"
#   @$(CP) "build/$(TARGET).bin" dist/

## Instead we'll just do these gross comment blocks..

EXTRA_C_DEFS ?=
TARGET_SUFFIX ?=

TARGET = dsy_bootloader_v$(VERSION_MAJ)_$(VERSION_MIN)${TARGET_SUFFIX}

# Sources
CPP_SOURCES = main.cpp \
$(wildcard ../shared/*.cpp)
#$(wildcard src/*.cpp)

# moved bootloader related files into the shared/ dir so other "custom" bootloaders are easier to manage..
#$(wildcard src/*.cpp)

USE_FATFS = 1

C_INCLUDES = -Isrc \
						 -I../shared

C_SOURCES += \
../cube_dfu/Middlewares/ST/STM32_USB_Device_Library/Class/DFU/Src/usbd_dfu.c \
../cube_dfu/USB_DEVICE/App/usbd_desc.c

C_INCLUDES += \
-I. \
-I../cube_dfu/Middlewares/ST/STM32_USB_Device_Library/Class/DFU/Inc \
-I../cube_dfu/USB_DEVICE/App

C_INCLUDES += $(EXTRA_C_DEFS)

LDSCRIPT = boot_linker.lds

# WARNING -- an opt of -O3 causes the jump to fail and makes USB connectivity unreliable
OPT = -Os
DEBUG=1

# CFLAGS += -DUSBD_SUPPORT_USER_STRING_DESC=1

# Library Locations
LIBDAISY_DIR = ../libDaisy

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
