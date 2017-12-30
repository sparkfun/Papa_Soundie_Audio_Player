.SUFFIXES: .img .bin .prg .spi .run .abs

all: spiall.spi progall.bin VS1000_B.RUN SDUPDATE.PRG sdupdatetester.bin

#Must be in decimal
#Because it's a SPI EEPROM, the offset does not need to be aligned.
#For convenience, set these to the same ones that are in VSIDE post-build!
SPIPLAYERSTART = 8192
SDPLAYERSTART  = 16384
SDMASSSTART    = 24576

VS1000LIB = lib
INCDIRS = -I . -I $(VS1000LIB)
LIBDIRS = -L . -L $(VS1000LIB)

LIBS  = -lc -ldev1000 -laudiomod
VCC   = vcc
FEATURES =
ASMFLAGS = -D ASM $(FEATURES)

COFF2BOOT = coff2allboot -i vs1000nand
COFF2SPIBOOT = coff2allboot -i vs1000spi
COMBINEIMG = combineimg

# For FreeBSD
ifeq ($(OSTYPE),FreeBSD)
VCCFLAGS = -P130 -g -O -fsmall-code $(FEATURES) \
	-DSPIPLAYERSTART=$(SPIPLAYERSTART) -DSDPLAYERSTART=$(SDPLAYERSTART) -DSDMASSSTART=$(SDMASSSTART)
RM = rm -f
CP = cp
else
ifeq ($(OSTYPE),linux)
# For Linux
VCCFLAGS = -P130 -g -O -fsmall-code $(FEATURES) \
	-DSPIPLAYERSTART=$(SPIPLAYERSTART) -DSDPLAYERSTART=$(SDPLAYERSTART) -DSDMASSSTART=$(SDMASSSTART)
RM = rm -f
CP = cp
else
# For Windows
VCCFLAGS = -P130 -O -fsmall-code $(FEATURES) \
	-DSPIPLAYERSTART=$(SPIPLAYERSTART) -DSDPLAYERSTART=$(SDPLAYERSTART) -DSDMASSSTART=$(SDMASSSTART)
RM = rm
CP = cp
endif
endif


.c.o:
	$(VCC) $(VCCFLAGS) $(INCDIRS) -o $@ $<

.s.o:
	vsa $(ASMFLAGS) $(INCDIRS) -o $@ $<

#Run program
.bin.run: Makefile
	vs3emu -chip vs1000 -s 115200 -l $< -c run.cmd

#rom1000.o: rom1000.txt
#	mkabs -o $@ -f $<

coff2spiboot: coff2spiboot.c
	gcc -Wall -o $@ $<

coff2BoOt: coff2BoOt.c
	gcc -Wall -o coff2BoOt coff2BoOt.c

#.bin.img: Makefile coff2BoOt
#	./coff2BoOt -c 2 -w 50 -x 0x50 $< $@

.bin.spi: Makefile
	$(COFF2SPIBOOT) -x 0x50 $< $@

.bin.prg: Makefile
	$(COFF2BOOT) -n -x 0x50 $< $@

.abs.o: Makefile
	mkabs -f $< -o $@

.o.bin:
	vslink -k -m mem_desc.vs1000 -o $@ $(VS1000LIB)/c-nand.o $< $(VS1000LIB)/rom1000.o $(LIBDIRS) $(LIBS)

usbmass.bin: usbmass.o jafterspi.o libaudiomod.a
	vslink -k -m mem_desc.vs1000 -o $@ $(VS1000LIB)/c-spi.o usbmass.o jafterspi.o $(VS1000LIB)/rom1000.o $(LIBDIRS) $(LIBS)

# With no NAND-Flash header
VS1000_B.RUN: flasher24.bin
	$(COFF2BOOT) -n -x 0x50 flasher24.bin VS1000_B.RUN

LIBOBJS = common.o uartasm.o uartctrl.o putstrp.o gpioctrl.o playwavorogg.o c-fatalloc2.o c-fatfindentry.o
libaudiomod.a: $(LIBOBJS)
	vsar ruv libaudiomod.a $(LIBOBJS)

playwavorogg.o: playwavorogg.c system.h
usbmass.o: usbmass.c Makefile system.h
spiplayer.o: spiplayer.c Makefile system.h uartctrl.h
	$(VCC) $(VCCFLAGS) $(INCDIRS) -o $@ $<
sdplayer.o: sdplayer.c Makefile system.h uartctrl.h
	$(VCC) $(VCCFLAGS) $(INCDIRS) -o $@ $<
uartasm.o: uartasm.s uartctrl.h
uartctrl.o: uartctrl.c uartctrl.h system.h
gpioctrl.o: gpioctrl.c gpioctrl.h system.h

spiplayer.bin: spiplayer.o libaudiomod.a
	vslink -k -m mem_desc.vs1000 -o $@ $(VS1000LIB)/c-spi.o spiplayer.o $(VS1000LIB)/rom1000.o $(LIBDIRS) $(LIBS)

sdplayer.bin: sdplayer.o libaudiomod.a
	vslink -k -m mem_desc.vs1000 -o $@ $(VS1000LIB)/c-spi.o sdplayer.o $(VS1000LIB)/rom1000.o $(LIBDIRS) $(LIBS)

spiall.spi: usbmass.spi spiplayer.spi sdplayer.spi sdmass.spi
	$(COMBINEIMG) -o spiall.spi -m 32768 +0 usbmass.spi +$(SPIPLAYERSTART) spiplayer.spi +$(SDPLAYERSTART) sdplayer.spi +$(SDMASSSTART) sdmass.spi
	echo >> spiall.spi

SDUPDATE.PRG: sdupdate.bin
	$(COFF2BOOT) -x 0x50 sdupdate.bin SDUPDATE.PRG

sdupdatetester.bin: sdupdatetester.o
	vslink -Mk -m mem_desc.vs1000 -o sdupdatetester.bin lib/c-nand.o sdupdatetester.o lib/rom1000.o -L . -L lib -lc -ldev1000 -laudiomod

#RAMDISK programming v2
#VS1000_B.RUN: usbupdate.bin
#	$(COFF2BOOT) -n -x 0x50 usbupdate.bin VS1000_B.RUN

clean:
	$(RM) -f *.o *.a *.bin *.img *~ Emulation-Debug/*

# Program firmware
program2M.o: program.c system.h
	$(VCC) $(VCCFLAGS) -D PROGRAM2M $(INCDIRS) -o $@ $<
program16M.o: program.c system.h
	$(VCC) $(VCCFLAGS) -D PROGRAM16M $(INCDIRS) -o $@ $<
program2M.bin: program2M.o system.h
program16M.bin: program2M.o system.h

program:
	@echo "Please run either 'make program2M' or 'make program16M'"
program2M: program2M.bin spiall.spi
	vs3emu -chip vs1000 -s 115200 -p 1 -l program2M.bin -c run.cmd
program16M: program16M.bin spiall.spi
	vs3emu -chip vs1000 -s 115200 -p 1 -l program16M.bin -c run.cmd
programany: program.bin spiall.spi
	vs3emu -chip vs1000 -s 115200 -p 1 -l program.bin -c run.cmd

# Program content from content.raw (FAT image)
content: content.bin
	vs3emu -chip vs1000 -s 115200 -p 1 -l content.bin -c run.cmd

# Dump content to dump.raw (FAT image)
dump: dump.bin
	vs3emu -chip vs1000 -s 115200 -p 1 -l dump.bin -c run.cmd

progall.run: spiall.spi

dist: clean
	(cd gpio ; gmake dist)
	zip audiomodule.zip Makefile *_desc* *.[chs] *.cmd *.bat *RUN *.spi *.exe *.abs SDUPDATE.PRG FIRMWARE.RAW lib/* docs/*.pdf README.TXT

installtovside:
	perl detabify.pl system.h >VS1000AudioModule/system.h
	perl detabify.pl usbmass.c >VS1000AudioModule/usbmass.c
	perl detabify.pl sdplayer.c >VS1000AudioModule/sdplayer.c
	perl detabify.pl spiplayer.c >VS1000AudioModule/spiplayer.c
	perl detabify.pl uartctrl.c >VS1000AudioModule/uartctrl.c
	perl detabify.pl uartctrl.h >VS1000AudioModule/uartctrl.h
	perl detabify.pl gpioctrl.c >VS1000AudioModule/gpioctrl.c
	perl detabify.pl gpioctrl.h >VS1000AudioModule/gpioctrl.h
	perl detabify.pl sdmass.c >VS1000AudioModule/sdmass.c
	perl detabify.pl sdupdate.c >VS1000AudioModule/sdupdate.c
	perl detabify.pl c-fatalloc2.c >VS1000AudioModule/c-fatalloc2.c
	perl detabify.pl c-fatfindentry.c >VS1000AudioModule/c-fatfindentry.c
	perl detabify.pl playwavorogg.c >VS1000AudioModule/playwavorogg.c
	cp Makefile README.TXT *.spi VS1000AudioModule/
	chmod u+w VS1000AudioModule/lib/*
	cp -p lib/* VS1000AudioModule/lib/
	rm -f VS1000AudioModule/lib/vsNand.h

diff:
	diff -wr . VS1000AudioModule | more
