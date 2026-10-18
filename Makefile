# Enter UCRT64 when invoked by native Windows make (for example in PowerShell).
ifeq ($(OS):$(MSYSTEM),Windows_NT:)
MSYS2_ROOT ?= C:/msys64
.PHONY: windows win64
windows win64:
	"$(MSYS2_ROOT)/usr/bin/bash.exe" -lc "export MSYSTEM=UCRT64; export PATH=/ucrt64/bin:/usr/bin:$$PATH; cd '$(CURDIR)'; make windows"
else
# Keep this list in alphabetical order for ease of reference.
.PHONY:           \
	all           \
	check         \
	clean         \
	configure     \
	distclean     \
	debug         \
	format        \
	meson         \
	osx           \
	purge         \
	release       \
	rom           \
	setup_debug   \
	setup_release \
	skrew         \
	skrewrm       \
	skrewup       \
	target        \
	update        \
	windows

ROM_REVISION ?= 1

SUBPROJ_DIR := subprojects

MESON_VER := 1.12.0
MESON_DIR := $(SUBPROJ_DIR)/meson-$(MESON_VER)

MESON ?= python3 $(MESON_DIR)/meson.py
NINJA ?= ninja
GIT ?= git

BUILD ?= build
BUILD_LINUX ?= $(BUILD)_linux
BUILD_OSX ?= $(BUILD)_osx
BUILD_WIN64 ?= $(BUILD)_win64
BUILD_NX ?= $(BUILD)_nx
ROOT_INI := $(BUILD)/root.ini
CACHE_LINUX := $(abspath $(BUILD_LINUX)/../.build/$(notdir $(BUILD_LINUX)))
ROOT_INI_LINUX := $(CACHE_LINUX)/root.ini
CACHE_OSX := $(abspath $(BUILD_OSX)/../.build/$(notdir $(BUILD_OSX)))
ROOT_INI_OSX := $(CACHE_OSX)/root.ini
CACHE_WIN64 := $(abspath $(BUILD_WIN64)/../.build/$(notdir $(BUILD_WIN64)))
ROOT_INI_WIN64 := $(CACHE_WIN64)/root.ini
ROOT_INI_NX := $(BUILD_NX)/root.ini

UNAME_R := $(shell uname -r)
UNAME_S := $(shell uname -s)
CWD := $(shell pwd)

GITHUB_CI = ${GITHUB_ACTIONS}

# Check for Windows-drive access
ifneq (,$(findstring Microsoft,$(UNAME_R)))
  ifneq (,$(filter /mnt/%,$(realpath $(CWD))))
    WSL_ACCESSING_WINDOWS := 0
  else
    WSL_ACCESSING_WINDOWS := 1
  endif
else
  WSL_ACCESSING_WINDOWS := 1
endif

# Set up the compiler toolchain dependency
SKREW_GET := tools/devtools/get_metroskrew.sh
SKREW_VER := 0.1.3
SKREW_DIR := tools/metroskrew

ifneq (,$(findstring Linux,$(UNAME_S)))
  ifeq (0,$(WSL_ACCESSING_WINDOWS))
    NATIVE := native.ini
    CROSS := cross.ini
    SKREW_SYS := windows
    SKREW_EXE := $(SKREW_DIR)/bin/skrewrap.exe
  else
    NATIVE := native.ini
    CROSS := cross_unix.ini
    SKREW_SYS := linux
    SKREW_EXE := $(SKREW_DIR)/bin/skrewrap
  endif
else
  ifneq (,$(findstring Darwin,$(UNAME_S)))
    NATIVE := native_macos.ini
    CROSS := cross_unix.ini
    SKREW_SYS := wine
    SKREW_EXE := $(SKREW_DIR)/bin/skrewrap
  else
    ifneq (,$(findstring BSD, $(UNAME_S)))
      NATIVE := native.ini
      CROSS := cross_unix.ini
      SKREW_SYS := linux
      SKREW_EXE := $(SKREW_DIR)/bin/skrewrap
    else
      NATIVE := native.ini
      CROSS := cross.ini
      SKREW_SYS := windows
      SKREW_EXE := $(SKREW_DIR)/bin/skrewrap.exe
    endif
  endif
endif

export NINJA_STATUS := [%p %f/%t] 

# Modders can delete the `check` dependency here after their first build.
all: release

.NOTPARALLEL: release
release: setup_release rom

.NOTPARALLEL: debug
debug: setup_debug rom
	$(NINJA) -C $(BUILD) debug.nef overlay.map

check: rom
	$(MESON) test -C $(BUILD)

rom: $(BUILD)/build.ninja
	$(NINJA) -C $(BUILD) pokeplatinum.us.nds

format: $(BUILD)/build.ninja
	$(NINJA) -C $(BUILD) clang-format

target: $(BUILD)/build.ninja
	$(MESON) compile -C $(BUILD) $(MESON_TARGET)

linux: export TMPDIR := $(CACHE_LINUX)/.build-temp
linux: export TMP := $(CACHE_LINUX)/.build-temp
linux: export TEMP := $(CACHE_LINUX)/.build-temp
linux: export APPIMAGE_EXTRACT_AND_RUN := 1
linux: $(CACHE_LINUX)/build.ninja
	mkdir -p "$(TMPDIR)"
	python3 tools/scripts/sync_libntr_patch.py
	$(MESON) setup --reconfigure --clearcache $(CACHE_LINUX)
	$(MESON) compile -C $(CACHE_LINUX)

osx: $(CACHE_OSX)/build.ninja
	python3 tools/scripts/sync_libntr_patch.py
	$(MESON) compile -C $(CACHE_OSX)

win64: $(CACHE_WIN64)/build.ninja
	python3 tools/scripts/sync_libntr_patch.py
	$(MESON) compile -C $(CACHE_WIN64)

windows: win64

nx: $(BUILD_NX)/build.ninja
	$(MESON) compile -C $(BUILD_NX)

clean: $(BUILD)/build.ninja
	$(MESON) compile -C $(BUILD) --clean
	rm -rf $(BUILD)/res

distclean:
	rm -rf $(BUILD)

purge: distclean
	rm -rf $(SKREW_DIR)
	! test -f $(MESON_DIR)/meson.py || $(MESON) subprojects purge --confirm
	rm -rf $(MESON_DIR)

update: meson skrewup
	$(MESON) subprojects update || true

setup_release: $(BUILD)/build.ninja
	$(MESON) configure $(BUILD) -Dgdb_debugging=false -Dlogging_enabled=false

setup_debug: $(BUILD)/build.ninja
	$(MESON) configure $(BUILD) -Dgdb_debugging=true -Dlogging_enabled=true

setup_linux: $(CACHE_LINUX)/build.ninja
	$(MESON) configure $(CACHE_LINUX) -Dbuild_target=linux

configure: $(BUILD)/build.ninja

$(BUILD)/build.ninja: | $(BUILD) $(SKREW_EXE) meson
	$(MESON) setup \
		-Drevision=$(ROM_REVISION) \
		--wrap-mode=nopromote \
		--native-file=meson/$(NATIVE) \
		--cross-file=meson/$(CROSS) \
		-- $(BUILD)

$(CACHE_LINUX)/build.ninja: $(ROOT_INI_LINUX) | $(BUILD_LINUX) $(SKREW_EXE) meson
	$(MESON) setup \
		-Dbuild_target=linux \
		--native-file=meson/$(NATIVE) \
		--native-file=$(ROOT_INI_LINUX) \
		--cross-file=meson/cross_pcport_linux.ini \
		--cross-file=$(ROOT_INI_LINUX) \
		-- $(CACHE_LINUX)

$(CACHE_OSX)/build.ninja: $(ROOT_INI_OSX) | $(BUILD_OSX) $(SKREW_EXE) meson
	$(MESON) setup \
		-Dbuild_target=osx \
		--force-fallback-for=yyjson \
		--native-file=meson/$(NATIVE) \
		--native-file=$(ROOT_INI_OSX) \
		-- $(CACHE_OSX)

ifeq ($(GITHUB_CI),)
$(CACHE_WIN64)/build.ninja: $(ROOT_INI_WIN64) | $(BUILD_WIN64) $(SKREW_EXE) meson
	$(MESON) setup \
		-Dbuild_target=win64 \
		--native-file=meson/$(NATIVE) \
		--native-file=$(ROOT_INI_WIN64) \
		--cross-file=meson/cross_pcport_win64.ini \
		--cross-file=$(ROOT_INI_WIN64) \
		-- $(CACHE_WIN64)
else
$(CACHE_WIN64)/build.ninja: $(ROOT_INI_WIN64) | $(BUILD_WIN64) $(SKREW_EXE) meson
	$(MESON) setup \
		-Dbuild_target=win64 \
		-Dskip_anim_scripts=true \
		--native-file=meson/$(NATIVE) \
		--native-file=$(ROOT_INI_WIN64) \
		--cross-file=meson/cross_pcport_win64.ini \
		--cross-file=$(ROOT_INI_WIN64) \
		-- $(CACHE_WIN64)
endif

$(BUILD_NX)/build.ninja: $(ROOT_INI_NX) | $(BUILD_NX) $(SKREW_EXE) meson
	$(MESON) setup \
		-Dbuild_target=nx \
		--native-file=meson/$(NATIVE) \
		--native-file=$(ROOT_INI_NX) \
		--cross-file=meson/cross_nx.ini \
		--cross-file=$(ROOT_INI_NX) \
		-- $(BUILD_NX)

$(ROOT_INI): | $(BUILD)
	echo "[constants]" > $@
	echo "root = '$$PWD'" >> $@

$(ROOT_INI_LINUX): | $(BUILD_LINUX)
	python3 tools/scripts/install_pc_config.py "$(BUILD_LINUX)" tools/config "$(CACHE_LINUX)"
	mkdir -p "$(CACHE_LINUX)/.build-temp"
	echo "[constants]" > $@
	echo "root = '$$PWD'" >> $@
	echo "[properties]" >> $@
	echo "release_dir = '$(abspath $(BUILD_LINUX))'" >> $@

$(ROOT_INI_OSX): | $(BUILD_OSX)
	python3 tools/scripts/install_pc_config.py "$(BUILD_OSX)" tools/config "$(CACHE_OSX)"
	mkdir -p "$(CACHE_OSX)/.build-temp"
	echo "[constants]" > $@
	echo "root = '$$PWD'" >> $@
	echo "[properties]" >> $@
	echo "release_dir = '$(abspath $(BUILD_OSX))'" >> $@

$(ROOT_INI_WIN64): | $(BUILD_WIN64)
	python3 tools/scripts/install_pc_config.py "$(BUILD_WIN64)" tools/config "$(CACHE_WIN64)"
	mkdir -p "$(CACHE_WIN64)/.build-temp"
	echo "[constants]" > $@
	echo "root = '$$PWD'" >> $@
	echo "[properties]" >> $@
	echo "release_dir = '$(abspath $(BUILD_WIN64))'" >> $@

$(ROOT_INI_NX): | $(BUILD_NX)
	echo "[constants]" > $@
	echo "root = '$$PWD'" >> $@

$(BUILD):
	mkdir -p -- $(BUILD)

$(BUILD_LINUX):
	mkdir -p -- $(BUILD_LINUX)

$(BUILD_OSX):
	mkdir -p -- $(BUILD_OSX)

$(BUILD_WIN64):
	mkdir -p -- $(BUILD_WIN64)

$(BUILD_NX):
	mkdir -p -- $(BUILD_NX) 

meson: $(MESON_DIR)/meson.py

$(MESON_DIR)/meson.py:
	$(GIT) clone --depth=1 -b $(MESON_VER) https://github.com/mesonbuild/meson $(@D)

skrew: $(SKREW_EXE)

skrewrm:
	rm -rf $(SKREW_DIR)

skrewup: skrewrm skrew

$(SKREW_EXE):
	SKREW_SYS=$(SKREW_SYS) SKREW_VER=$(SKREW_VER) SKREW_DIR=$(SKREW_DIR) $(SKREW_GET)

endif
