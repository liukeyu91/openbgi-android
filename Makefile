# This file is part of OpenBGI (https://openbgi.net).
# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile - builds OpenBGI
#
#   make                 native Linux build        -> $(BIN_DIR)/bgi
#   make win             MinGW cross build          -> $(BIN_DIR)/bgi.exe
#   make android-core    Android ARM64 core archive -> $(BIN_DIR)/android/arm64-v8a/libopenbgi-core.a
#   make android-native  Android SurfaceView/JNI library -> $(BIN_DIR)/android/arm64-v8a/libopenbgi.so
#   make wasm            Emscripten build           -> $(BIN_DIR)/wasm/openbgi.html (+ .js, .wasm; docs/wasm.md)
#   make CC=clang        any other C99 compiler
#   make tools           the command-line tools -> $(BIN_DIR)/bpasm, $(BIN_DIR)/bgiscn, $(BIN_DIR)/bgidump, $(BIN_DIR)/bgipack
#   make test            build and run the unit tests (tests/)
#   make check           syntax-check every source with -pedantic
#   make check-win       syntax-check src/os/win32 against stub SDK headers (no MinGW needed)
#   make check-wasm      syntax-check src/os/wasm against a stub Emscripten header (no emcc needed)
#   make wasm-test       the asset server's protocol test (node)
#   make wasm-sim        the WebAssembly back end as a native binary ($(BIN_DIR)/openbgi-wasmsim, no browser)
#   make clean
#
# Layout: inc/ headers, src/ sources, $(OBJ_DIR)/ objects, $(BIN_DIR)/ binaries.
# The OS layer is src/os/os_common.c plus src/os/posix/*.c, src/os/win32/*.c
# or src/os/wasm/*.c.  Only plain make is used; no autotools / CMake.

CC      ?= gcc
OBJ_DIR ?= obj
BIN_DIR ?= bin
SANITIZERS ?=
comma := ,
ifneq ($(strip $(SANITIZERS)),)
  OBJ_DIR := obj/sanitize-$(subst $(comma),-,$(SANITIZERS))
  BIN_DIR := bin/sanitize-$(subst $(comma),-,$(SANITIZERS))
  override OPT := -O1 -g
  CFLAGS += -fsanitize=$(SANITIZERS) -fno-omit-frame-pointer -fno-sanitize-recover=all
  LDFLAGS += -fsanitize=$(SANITIZERS) -fno-sanitize-recover=all
endif
# the optional libraries are looked up with pkg-config; a cross build uses
# its toolchain's own (see the win32 platform below), never the host's
PKG_CONFIG ?= pkg-config
CSTD    := -std=c99
# -Wno-format-truncation / -Wno-format-overflow: the engine formats paths and
# messages into fixed MAX_PATH-sized buffers as the original does; GCC's
# estimates assume unbounded %s arguments (the inputs are archive and file
# names, which are bounded by the formats that produce them)
WARN    := -Wall -Wextra -pedantic -Wno-unused-parameter \
           -Wno-format-truncation -Wno-format-overflow -Wno-unknown-warning-option
OPT     ?= -O2 -g
INC     := -Iinc
CFLAGS  += $(CSTD) $(WARN) $(OPT) $(INC) -D_POSIX_C_SOURCE=200809L
CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS += $(EXTRA_LDFLAGS)

# ---- platform -------------------------------------------------------------
# "make win" selects MinGW, "make wasm" Emscripten, and "make android-core"
# the Android NDK cross compiler.
PLATFORM ?= posix
ifeq ($(PLATFORM),wasm)
  CC      := emcc
  EXE     := .html
  OS_SRC  := $(wildcard src/os/wasm/*.c)
  CFLAGS  += -DBGI_WASM
  OPT     := -O2
  # Asyncify lets the engine's blocking calls (file reads over the websocket,
  # sleeps) suspend into the browser's event loop; its stack holds the locals
  # of the suspended frames.  IDBFS persists the overlay (what the engine
  # writes) in IndexedDB.  The page is tools/wasm/shell.html, the websocket
  # client tools/wasm/client.js.
  LDFLAGS += $(OPT) -sASYNCIFY -sASYNCIFY_STACK_SIZE=262144 \
             -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=2147483648 \
             -sENVIRONMENT=web -sEXIT_RUNTIME=0 -sINVOKE_RUN=1 \
             -sEXPORTED_FUNCTIONS=_main,_malloc,_free \
             -sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPU8,HEAPU16,HEAP16,HEAPU32,FS,IDBFS \
             -lidbfs.js --pre-js tools/wasm/client.js --shell-file tools/wasm/shell.html
  TARGET  := $(BIN_DIR)/wasm/openbgi$(EXE)
  # no host libraries: the BW codec 3 (Vorbis) needs the single-file decoder here
  VORBIS  := $(if $(wildcard src/snd/stb_vorbis.c),stb,none)
else ifeq ($(PLATFORM),wasmsim)
  # "make wasm-sim": the WebAssembly back end on the host, with
  # tools/wasm/fakebrowser.c and hostmock.c in place of the browser and the
  # asset server (docs/wasm.md); a native binary that runs a game directory
  # through the back end's own file layer, overlay and presentation
  EXE     :=
  OS_SRC  := $(wildcard src/os/wasm/*.c)
  TOOL_SRC := tools/wasm/hostmock.c tools/wasm/fakebrowser.c
  CFLAGS  += -DBGI_WASM -DBGI_POSIX -I. -Itools/wasmstub
  LDLIBS  += -lm
  TARGET  := $(BIN_DIR)/openbgi-wasmsim
  VORBIS  := none
else ifeq ($(PLATFORM),win32)
  # CROSS is the toolchain prefix; it must not be given a value (not even an
  # empty one) before this line, or "?=" keeps that and the host's gcc builds
  CROSS   ?= i686-w64-mingw32-
  CC      := $(CROSS)gcc
  PKG_CONFIG := $(CROSS)pkg-config
  EXE     := .exe
  OS_SRC  := $(wildcard src/os/win32/*.c)
  LDLIBS  += -lgdi32 -luser32 -lwinmm -limm32 -lcomctl32 -lole32 -loleaut32 -lshell32 -ladvapi32 -lcomdlg32 -luuid -lstrmiids
  CFLAGS  += -DBGI_WIN32
  LDFLAGS += -mwindows
else ifneq ($(filter android-core android-native,$(PLATFORM)),)
  ifeq ($(strip $(ANDROID_NDK_HOME)),)
    $(error Set ANDROID_NDK_HOME to the Android NDK directory)
  endif
  ANDROID_API ?= 28
  ANDROID_HOST_TAG ?= linux-x86_64
  ANDROID_LLVM := $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/$(ANDROID_HOST_TAG)/bin
  CC := $(ANDROID_LLVM)/aarch64-linux-android$(ANDROID_API)-clang
  AR := $(ANDROID_LLVM)/llvm-ar
  CFLAGS += -DBGI_ANDROID -D_GNU_SOURCE -fPIC -Wno-unknown-warning-option
  VORBIS := none
  TARGET := $(BIN_DIR)/android/arm64-v8a/libopenbgi-core.a
  ifeq ($(PLATFORM),android-native)
    ifeq ($(and $(strip $(FREETYPE_INCLUDE)),$(strip $(FREETYPE_LIB))),)
      $(error Set FREETYPE_INCLUDE and FREETYPE_LIB to the ARM64 FreeType headers and static library)
    endif
    ifeq ($(strip $(ANDROID_VORBIS_PREFIX)),)
      $(error Set ANDROID_VORBIS_PREFIX to the installation directory of ARM64 Ogg and Vorbis static libraries)
    endif
    ANDROID_POSIX := audio files font machine native paths process registry thread time
    OS_SRC := $(wildcard src/os/android/*.c) $(addprefix src/os/posix/,$(addsuffix .c,$(ANDROID_POSIX)))
    CFLAGS += -I$(FREETYPE_INCLUDE)
    LDLIBS += $(FREETYPE_LIB) -landroid -laaudio -llog -lEGL -lGLESv2 -lm
    ANDROID_VORBIS_ARCHIVES := $(addprefix $(ANDROID_VORBIS_PREFIX)/lib/,libvorbisfile.a libvorbis.a libogg.a)
    VORBIS := vorbisfile
    VORBIS_CFLAGS := -I$(ANDROID_VORBIS_PREFIX)/include
    VORBIS_LIBS := $(ANDROID_VORBIS_ARCHIVES) -lm
    TARGET := $(BIN_DIR)/android/arm64-v8a/libopenbgi.so
  endif
else
  EXE     :=
  OS_SRC  := $(wildcard src/os/posix/*.c)
  LDLIBS  += -lX11 -lXext -lfreetype -lpthread -lm
  CFLAGS  += -DBGI_POSIX $(shell $(PKG_CONFIG) --cflags freetype2 2>/dev/null)
  # ALSA output when the development files are present; silence otherwise
  ifneq ($(shell $(PKG_CONFIG) --exists alsa 2>/dev/null && echo yes),)
    CFLAGS += -DBGI_HAVE_ALSA $(shell $(PKG_CONFIG) --cflags alsa)
    LDLIBS += $(shell $(PKG_CONFIG) --libs alsa)
  endif
  # fontconfig resolves the face names; without it well-known font files are searched
  ifneq ($(shell $(PKG_CONFIG) --exists fontconfig 2>/dev/null && echo yes),)
    CFLAGS += -DBGI_HAVE_FONTCONFIG $(shell $(PKG_CONFIG) --cflags fontconfig)
    LDLIBS += $(shell $(PKG_CONFIG) --libs fontconfig)
  endif
endif

# Development sanitizers use separate objects and binaries; they never reuse
# unsanitized release objects or add sanitizer runtimes to release APKs.
ifneq ($(strip $(SANITIZERS)),)
  ifneq ($(PLATFORM),posix)
    $(error SANITIZERS currently supports the POSIX test backend only)
  endif
endif
HARDEN ?= 1
ifeq ($(HARDEN),1)
  ifeq ($(filter wasm wasmsim win32,$(PLATFORM)),)
    CFLAGS += -fstack-protector-strong -D_FORTIFY_SOURCE=2 -Wformat=2 -Werror=format-security
    LDFLAGS += -Wl,-z,relro,-z,now,-z,noexecstack
  endif
endif

# ---- Vorbis (BW codec 3) -------------------------------------------------------
# VORBIS=auto (default) uses libvorbisfile when pkg-config finds it, else
# src/snd/stb_vorbis.c when that file has been dropped in, else the sounds
# of that codec play as silence.  VORBIS=vorbisfile / stb / none force one.
VORBIS ?= auto
ifeq ($(VORBIS),auto)
  ifneq ($(shell $(PKG_CONFIG) --exists vorbisfile 2>/dev/null && echo yes),)
    VORBIS := vorbisfile
  else ifneq ($(wildcard src/snd/stb_vorbis.c),)
    VORBIS := stb
  else
    VORBIS := none
  endif
endif
ifeq ($(VORBIS),vorbisfile)
  VORBIS_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags vorbisfile 2>/dev/null)
  VORBIS_LIBS ?= $(shell $(PKG_CONFIG) --libs vorbisfile 2>/dev/null || echo -lvorbisfile -lvorbis -logg)
  CFLAGS += -DBGI_HAVE_VORBISFILE $(VORBIS_CFLAGS)
  LDLIBS += $(VORBIS_LIBS)
endif
ifeq ($(VORBIS),stb)
  CFLAGS += -DBGI_USE_STB_VORBIS
endif

# ---- sources ----------------------------------------------------------------
CORE_SRC := $(wildcard src/core/*.c)
ASM_SRC  := $(wildcard src/asm/*.c)
SCN_SRC  := $(wildcard src/scn/*.c)
BPC_SRC  := $(wildcard src/bpc/*.c)
# the dump / pack tools' module (docs/dump.md) is not part of the WebAssembly
# build: it works on directories of the host
DUMP_SRC := $(if $(filter wasm wasmsim,$(PLATFORM)),,$(wildcard src/dump/*.c))
VM_SRC   := $(wildcard src/vm/*.c)
WAIT_SRC := $(wildcard src/wait/*.c)
SYS_SRC  := $(wildcard src/sys/*.c) $(wildcard src/sys/install/*.c) $(wildcard src/sysobj/*.c)
GFX_SRC  := $(wildcard src/gfx/*.c) $(wildcard src/gfx/background/*.c) $(wildcard src/gfx/mgr/*.c) \
            $(wildcard src/gfx/particle/*.c) $(wildcard src/gfx/window/*.c) $(wildcard src/gfx/text/*.c)
SND_SRC  := $(wildcard src/snd/*.c) $(wildcard src/snd/bw/*.c) $(wildcard src/snd/wm/*.c)
# the debugger (docs/debugger.md) is not part of the WebAssembly build: the
# hooks in the engine compile to nothing there (inc/bgi/dbg.h)
DBG_SRC  := $(if $(filter wasm wasmsim,$(PLATFORM)),,$(wildcard src/dbg/*.c) $(wildcard src/dbg/views/*.c))
COMMON_OS_SRC := src/os/os_common.c

SRC := $(CORE_SRC) $(ASM_SRC) $(SCN_SRC) $(BPC_SRC) $(DUMP_SRC) $(VM_SRC) $(WAIT_SRC) $(SYS_SRC) $(GFX_SRC) $(SND_SRC) $(DBG_SRC) \
       $(COMMON_OS_SRC) $(OS_SRC)
OBJ := $(patsubst src/%.c,$(OBJ_DIR)/$(PLATFORM)/%.o,$(SRC)) $(patsubst tools/%.c,$(OBJ_DIR)/$(PLATFORM)/tools/%.o,$(TOOL_SRC))
DEP := $(OBJ:.o=.d)
LIB_OBJ := $(filter-out $(OBJ_DIR)/$(PLATFORM)/sys/main.o,$(OBJ)) # the engine without its entry point

# Opt-in device harness: a separate APK/library; never part of the release engine.
ifeq ($(PLATFORM):$(ANDROID_HOST_TEST),android-native:1)
  LIB_OBJ += $(OBJ_DIR)/android-native/tools/android/hosttest.o
  TARGET := $(BIN_DIR)/android-hosttest/arm64-v8a/libopenbgi.so
  ANDROID_HOST_WRAP := -Wl,--wrap=Engine_Main
endif

TARGET ?= $(BIN_DIR)/bgi$(EXE)

.PHONY: all win android-core android-native wasm wasm-sim tools check check-win check-wasm wasm-test test sanitizer-test clean msg

all: $(TARGET)

# ---- the command-line tools (tools/*.c linked with the engine library) ----
TOOLS := $(BIN_DIR)/bpasm$(EXE) $(BIN_DIR)/bgiscn$(EXE) $(BIN_DIR)/bgidump$(EXE) $(BIN_DIR)/bgipack$(EXE)

tools: $(TOOLS)

$(BIN_DIR)/bpasm$(EXE): tools/bpasm.c $(LIB_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB_OBJ) $(LDLIBS)

$(BIN_DIR)/bgiscn$(EXE): tools/bgiscn.c $(LIB_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB_OBJ) $(LDLIBS)

$(BIN_DIR)/bgidump$(EXE): tools/bgidump.c $(LIB_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB_OBJ) $(LDLIBS)

$(BIN_DIR)/bgipack$(EXE): tools/bgipack.c $(LIB_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB_OBJ) $(LDLIBS)

win:
	$(MAKE) PLATFORM=win32

android-core:
	$(MAKE) PLATFORM=android-core

android-native:
	$(MAKE) PLATFORM=android-native

wasm:
	$(MAKE) PLATFORM=wasm

wasm-sim:
	$(MAKE) PLATFORM=wasmsim

ifeq ($(PLATFORM),android-core)
$(TARGET): $(LIB_OBJ) | $(BIN_DIR)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $(LIB_OBJ)
else ifeq ($(PLATFORM),android-native)
$(filter $(OBJ_DIR)/android-native/snd/bw/%,$(OBJ)): $(ANDROID_VORBIS_ARCHIVES)
$(TARGET): $(LIB_OBJ) $(FREETYPE_LIB) $(ANDROID_VORBIS_ARCHIVES) | $(BIN_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(ANDROID_HOST_WRAP) -shared -Wl,--no-undefined -Wl,-soname,libopenbgi.so -Wl,-z,max-page-size=16384 -o $@ $(LIB_OBJ) $(LDLIBS)
else
$(TARGET): $(OBJ) | $(BIN_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)
endif

$(OBJ_DIR)/$(PLATFORM)/%.o: src/%.c | inc/bgi/msg.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(OBJ_DIR)/$(PLATFORM)/tools/%.o: tools/%.c | inc/bgi/msg.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# third-party single-file libraries are compiled without the project's warnings
$(OBJ_DIR)/$(PLATFORM)/snd/stb_vorbis.o: src/snd/stb_vorbis.c
	@mkdir -p $(dir $@)
	$(CC) $(CSTD) $(OPT) $(INC) -w -c -o $@ $<

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

# the Shift-JIS message table is generated from tools/messages.txt
inc/bgi/msg.h: tools/messages.txt tools/mkmsg.py
	python3 tools/mkmsg.py

msg: inc/bgi/msg.h

# syntax check of every translation unit (both platforms share all but src/os)
check:
	@for f in $(SRC); do \
	    echo "  CHECK $$f"; \
	    $(CC) $(CFLAGS) -fsyntax-only $$f || exit 1; \
	done

# syntax check of the Win32 back end without MinGW, against the stub SDK
# headers in tools/win32stub/ (see docs/building.md)
check-win:
	python3 tools/win32stub/gen.py

# syntax check of the WebAssembly back end without emcc, against the stub
# Emscripten header in tools/wasmstub/ (the JavaScript bodies are dropped)
check-wasm:
	@for f in src/os/wasm/*.c; do \
	    echo "  CHECK $$f"; \
	    $(CC) $(CFLAGS) $(LDFLAGS) -DBGI_WASM -Itools/wasmstub -fsyntax-only $$f || exit 1; \
	done

# the asset server's protocol, exercised through the browser-side client under node
wasm-test:
	node tools/wasm/selftest.js

# ---- tests ---------------------------------------------------------------------
TEST_SRC := $(filter-out tests/wasmfs.c tests/win32wide.c tests/posixpaths.c,$(wildcard tests/*.c))
TEST_BIN := $(patsubst tests/%.c,$(BIN_DIR)/test_%$(EXE),$(TEST_SRC))

$(BIN_DIR)/test_%$(EXE): tests/%.c $(LIB_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB_OBJ) $(LDLIBS)

# Count actual decoder calls while exercising cache behavior with real images.
$(BIN_DIR)/test_cbgcache$(EXE): tests/cbgcache.c $(LIB_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Wl,--wrap=BGI_CbgDecode -o $@ $< $(LIB_OBJ) $(LDLIBS)

$(BIN_DIR)/test_cbgfast$(EXE): tests/cbgfast.c src/core/cbg.c $(LIB_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(filter-out $(OBJ_DIR)/$(PLATFORM)/core/cbg.o,$(LIB_OBJ)) $(LDLIBS)

$(BIN_DIR)/test_cbg2fast$(EXE): tests/cbg2fast.c src/core/cbg2.c $(LIB_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(filter-out $(OBJ_DIR)/$(PLATFORM)/core/cbg2.o,$(LIB_OBJ)) $(LDLIBS)

# the WebAssembly file layer on the host, against the mock server of the test
# (built apart: it defines the OS_File* functions the platform's back end has too)
WASMFS_SRC := tests/wasmfs.c src/os/wasm/fs.c src/os/wasm/sjis.c src/os/wasm/host.c src/os/os_common.c \
              tools/wasm/hostmock.c
$(BIN_DIR)/test_wasmfs$(EXE): $(WASMFS_SRC) inc/bgi/os_wasm.h | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -DBGI_WASM -I. -Itools/wasmstub -o $@ $(WASMFS_SRC)

# the Win32 back end's string conversions on the host: wide.c against the stub
# windows.h, with the 16-bit wchar_t of Windows, the code pages mocked by the
# test with iconv (built apart for the same reason as the test above)
WIN32WIDE_SRC := tests/win32wide.c src/os/win32/wide.c src/os/os_common.c
$(BIN_DIR)/test_win32wide$(EXE): $(WIN32WIDE_SRC) inc/bgi/os_win32.h tools/win32stub/windows.h | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -DBGI_WIN32 -fshort-wchar -Itools/win32stub -o $@ $(WIN32WIDE_SRC)

ifeq ($(PLATFORM),posix)
# Compile the Android font/encoding paths without Android window or JNI services.
ANDROIDFONT_SRC := tools/tests/androidfont.c src/os/posix/font.c src/os/android/sjis.c src/os/os_common.c
$(BIN_DIR)/test_androidfont$(EXE): $(ANDROIDFONT_SRC) inc/bgi/os_posix.h inc/bgi/os.h inc/bgi/os_common.h src/os/android/cp932.inc src/os/cp936.inc | $(BIN_DIR)
	$(CC) $(filter-out -DBGI_HAVE_FONTCONFIG,$(CFLAGS)) $(LDFLAGS) -DBGI_ANDROID -o $@ $(ANDROIDFONT_SRC) $(LDLIBS)
TEST_BIN += $(BIN_DIR)/test_androidfont$(EXE)

ANDROIDOUTPUT_SRC := tools/tests/androidoutput.c src/os/android/audio_output.c
$(BIN_DIR)/test_androidoutput$(EXE): $(ANDROIDOUTPUT_SRC) src/os/android/audio_output.h src/os/android/viewport.h tools/android/tests/stubs/aaudio/AAudio.h | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -Itools/android/tests/stubs -o $@ $(ANDROIDOUTPUT_SRC) -lm
TEST_BIN += $(BIN_DIR)/test_androidoutput$(EXE)

$(BIN_DIR)/test_posixpaths$(EXE): tests/posixpaths.c src/os/posix/paths.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -pthread
TEST_BIN += $(BIN_DIR)/test_wasmfs$(EXE) $(BIN_DIR)/test_win32wide$(EXE) $(BIN_DIR)/test_posixpaths$(EXE)
endif

test: $(TEST_BIN)
	@status=0; for t in $(TEST_BIN); do echo "  RUN $$t"; $$t || status=1; done; exit $$status

sanitizer-test:
	$(MAKE) SANITIZERS=address,undefined test

clean:
	python3 tools/clean.py "$(OBJ_DIR)" "$(BIN_DIR)"

-include $(DEP)
