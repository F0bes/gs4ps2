EE_BIN = gs4ps2.elf
EE_OBJS = gs4ps2.o network.o common.o server.o gs_glue.o config.o
EE_OBJS += irx/ps2dev9_irx.o irx/netman_irx.o irx/ps2ip_irx.o irx/smap_irx.o
EE_LIBS = -lkernel -lpatches -lgraph -ldma -lps2ip -lnetman

# Git version
GIT_VERSION := "$(shell git describe --abbrev=4 --always --tags)"

EE_CFLAGS = -I$(shell pwd) -Wno-strict-aliasing -Werror  -DGIT_VERSION="\"$(GIT_VERSION)\""

all: $(EE_BIN)

irx/ps2dev9_irx.c: $(PS2SDK)/iop/irx/ps2dev9.irx
	bin2c $< irx/ps2dev9_irx.c ps2dev9_irx

irx/netman_irx.c: $(PS2SDK)/iop/irx/netman.irx
	bin2c $< irx/netman_irx.c netman_irx

irx/ps2ip_irx.c: $(PS2SDK)/iop/irx/ps2ip.irx
	bin2c $< irx/ps2ip_irx.c ps2ip_irx

irx/smap_irx.c: $(PS2SDK)/iop/irx/smap.irx
	bin2c $< irx/smap_irx.c smap_irx


clean:
	rm -f $(EE_OBJS)

run: $(EE_BIN)
	ps2client execee host:$(EE_BIN)

wsl: $(EE_BIN)
	$(PCSX2) --elf="$(shell wslpath -w $(shell pwd))/$(EE_BIN)"

emu: $(EE_BIN)
	$(PCSX2) --elf="$(shell pwd)/$(EE_BIN)"

reset:
	ps2client reset
	ps2client netdump

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
