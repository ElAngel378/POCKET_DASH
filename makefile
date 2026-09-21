# GBDK path - use environment variable if set, otherwise default to C:/gbdk
GBDK ?= C:/gbdk
GBCC = $(GBDK)/bin/lcc

PROJECT_NAME = POCKETDASH
SRCDIR = src
INCDIR = include
TEMPDIR = temp
BINDIR = bin
LIBDIR = lib

SRCS = $(wildcard $(SRCDIR)/*.c) $(wildcard $(SRCDIR)/*/*.c)
OBJS = $(foreach src, $(SRCS), $(TEMPDIR)/$(notdir $(src:.c=.o)))

vpath %.c $(SRCDIR) $(SRCDIR)/graphics $(SRCDIR)/music $(SRCDIR)/sprites $(SRCDIR)/levels $(SRCDIR)/sfx

# SDCC optimizes almost nothing by default, and lcc has no -O option
# (unrecognized options go to the linker!). Compiler flags must be forwarded
# via -Wf. Without these the game cannot hold 60fps: all OAM/metatile work
# runs at naive -O0 speed.
# -Wl-yp0x143=0x80 enables GBC support in the ROM header
LCCFLAGS = -I$(INCDIR) -Isrc/graphics -Wf--opt-code-speed -Wf--max-allocs-per-node1000000 -Wa-I. -Wl-j -Wl-yt0x1B -Wl-yo256 -Wl-ya1 -Wl-yp0x143=0x80
LIBS = $(LIBDIR)/hUGEDriver.lib

all: prepare $(BINDIR)/$(PROJECT_NAME).gb

prepare:
	@mkdir -p $(TEMPDIR)
	@mkdir -p $(BINDIR)

$(TEMPDIR)/%.o: %.c
	$(GBCC) $(LCCFLAGS) -c -o $@ $<

$(BINDIR)/$(PROJECT_NAME).gb: $(OBJS)
	$(GBCC) $(LCCFLAGS) -o $@ $(OBJS) $(LIBS)

clean:
	-rm -rf $(TEMPDIR) $(BINDIR)
