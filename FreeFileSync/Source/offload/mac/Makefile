# macOS app for Offload mode.
#
#   make            build Offload.app (arm64, ad-hoc signed)
#   make run        build and open it
#   make screenshot render the window offscreen to offload-window.png
#   make clean
#
# Needs the engine archive first:
#   make -C .. -f Makefile.macos liboffloadapi.a

SWIFTC     := /usr/bin/swiftc
ENGINE_DIR := ../..
ENGINE_LIB := $(ENGINE_DIR)/liboffloadapi.a
SDK        := $(shell xcrun --show-sdk-path)
ARCH       := $(shell uname -m)
TARGET     := $(ARCH)-apple-macos13.0
BREW_LLVM  := $(shell brew --prefix llvm 2>/dev/null)

SOURCES := Entry.swift Theme.swift Engine.swift Model.swift Views.swift Demo.swift UITest.swift

# The engine calls into glib (UTF-8 normalisation), CoreFoundation (week start)
# and wx's translation shim; the same libraries the CLI links against.
ENGINE_LIBS := -L$(BREW_LLVM)/lib/c++ -Xlinker -rpath -Xlinker $(BREW_LLVM)/lib/c++ \
               $(shell pkg-config --libs glib-2.0 gio-2.0 gobject-2.0 2>/dev/null) \
               $(shell wx-config --libs std 2>/dev/null) \
               -framework CoreFoundation -lc++

SWIFTFLAGS := -O -sdk $(SDK) -target $(TARGET) -import-objc-header bridge.h -parse-as-library

all: Offload.app

Offload: $(SOURCES) bridge.h $(ENGINE_LIB)
	$(SWIFTC) $(SWIFTFLAGS) $(SOURCES) $(ENGINE_LIB) $(ENGINE_LIBS) -o $@

Offload.app: Offload Info.plist
	rm -rf $@
	mkdir -p $@/Contents/MacOS $@/Contents/Resources
	cp Offload $@/Contents/MacOS/Offload
	cp Info.plist $@/Contents/Info.plist
	cp ../capi.h $@/Contents/Resources/ 2>/dev/null || true
	codesign --force --sign - $@ 2>/dev/null || echo "codesign skipped"

run: Offload.app
	open Offload.app

screenshot: Offload
	./Offload --render offload-window.png

# Drives the real window with real CGEvents. Needs Accessibility permission for
# the app that runs this (System Settings -> Privacy & Security -> Accessibility).
clickdriver: clickdriver.swift
	$(SWIFTC) -O -sdk $(SDK) -target $(TARGET) clickdriver.swift -o $@

ui-test: Offload clickdriver
	@rm -f /tmp/offload-ui.json /tmp/offload-ui.json.png
	@./Offload --ui-serve /tmp/offload-ui.json & app=$$!; \
	 sleep 1.5; \
	 ./clickdriver --state /tmp/offload-ui.json; rc=$$?; \
	 sleep 0.4; \
	 kill $$app 2>/dev/null; wait $$app 2>/dev/null; \
	 cp -f /tmp/offload-ui.json.png offload-window-live.png 2>/dev/null; \
	 exit $$rc

test: Offload
	./Offload --fit-test
	@echo "(transfer tests need a source folder: ./Offload --self-test SRC DEST [DEST...])"

clean:
	rm -rf Offload Offload.app clickdriver offload-window*.png

.PHONY: all run screenshot test ui-test clickdriver clean
