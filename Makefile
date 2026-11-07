PS3DEV     ?= /usr/local/ps3dev
TARGET     := PS3ToolsKit
TITLE      := PS3 Tools Kit
APPID      := PSTK00001
CONTENTID  := UP0001-$(APPID)_00-0000000000000000
APPVERSION = 02.00

SOURCES := $(wildcard source/*.c)
HEADERS := $(wildcard source/*.h)

CC      := ppu-gcc
MAKE_SELF        ?= make_self
MAKE_SELF_NPDRM  ?= make_self_npdrm
PYTHON           ?=
SFO_PY           ?= sfo.py
PKG_PY           ?= pkg.py
SFO_XML          ?= $(PS3DEV)/bin/sfo.xml
CFLAGS  := -O2 -Wall -mcpu=cell \
           -I$(PS3DEV)/ppu/include \
           -I$(PS3DEV)/portlibs/ppu/include \
           -I$(PS3DEV)/portlibs/ppu/include/freetype2
LDFLAGS := -L$(PS3DEV)/ppu/lib \
           -L$(PS3DEV)/portlibs/ppu/lib \
           -L$(PS3DEV)/ppu/powerpc64-ps3-elf/lib
CAIRO_DIR := lto-cairo
LIBS    := -lpixman-1 -lfreetype -lpng -lz -lm \
           -laudio -lnetctl -lnet -lsysmodule -lgcm_sys -lrsx -lsysutil -lsysfs -lio -lrt -llv2

.PHONY: all pkg clean

all: $(TARGET).self

$(CAIRO_DIR)/.done:
	mkdir -p $(CAIRO_DIR) && cd $(CAIRO_DIR) && ppu-ar x $(PS3DEV)/portlibs/ppu/lib/libcairo.a && touch .done

$(TARGET).elf: $(SOURCES) $(HEADERS) $(CAIRO_DIR)/.done
	$(CC) $(CFLAGS) $(SOURCES) -flto $(CAIRO_DIR)/*.o -o $(TARGET)_debug.elf $(LDFLAGS) $(LIBS)
	ppu-strip $(TARGET)_debug.elf -o $(TARGET).elf
	sprxlinker $(TARGET).elf

$(TARGET).self: $(TARGET).elf
	$(MAKE_SELF) $(TARGET).elf $(TARGET).self

pkg: $(TARGET).elf
	rm -rf build && mkdir -p build/pkg/USRDIR
	$(MAKE_SELF_NPDRM) $(TARGET).elf build/pkg/USRDIR/EBOOT.BIN $(CONTENTID)
	cp pkgfiles/ICON0.PNG pkgfiles/PIC1.PNG build/pkg/
	cp pkgfiles/USRDIR/* build/pkg/USRDIR/
	cp $(SFO_XML) build/sfo.xml
	sed -i "s/01\.00/$(APPVERSION)/g" build/sfo.xml
	$(PYTHON) $(SFO_PY) --title "$(TITLE)" --appid "$(APPID)" -f build/sfo.xml build/pkg/PARAM.SFO
	$(PYTHON) $(PKG_PY) --contentid $(CONTENTID) build/pkg/ $(TARGET).pkg
	@echo "==> $(TARGET).pkg est prêt"

clean:
	rm -rf build $(CAIRO_DIR) *.elf *.self *.pkg