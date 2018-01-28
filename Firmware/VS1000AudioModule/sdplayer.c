 /*
  Copyright 2008-2013 VLSI Solution Oy. Absolutely no warranty.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vs1000.h>
#include <audio.h>
#include <mappertiny.h>
#include <minifat.h>
#include <codec.h>
#include <vsNand.h>
#include <player.h>
#include <usblowlib.h>

#include "system.h"

//#define TEST_WRITE /* modifies config.txt */


#ifdef UART_CONTROL
#include "uartctrl.h"
#endif/*UART_CONTROL*/
#ifdef GPIO_CONTROL
#include "gpioctrl.h"
#endif /*GPIO_CONTROL*/

#ifdef USE_QUEUE
#define QUEUE_SIZE 100
s_int16 queued = 0;
u_int16 queue[QUEUE_SIZE];
#endif/*USE_QUEUE*/

#define USE_USB /* You get a bit more free code space if you disable USB. */
#ifdef DISABLE_USB
#undef USE_USB
#endif

//#undef USE_WAV
//#define USE_DEBUG
//#define DEBUG_LEVEL 3

#define USE_HC      /* Support SD-HC cards. */

// The VLSI RDY LED on the PAPA soundie is GPIO pin 5
#define VLSI_RDY_LED (32)


#include <dev1000.h>

/*
  TODO: use multiple block read and multiple block write
  TODO: readahead using multiple block read?
  TODO: multiple block write would need error handling, possibly rewrites

  Start Block Tokens and Stop Tran Token:
  Single block read, single block write, multiple block read:
  - start block 0xfe
  Multiple block write (CMD25):
  - start block 0xfc
  - stop transmission 0xfd

  Multiple block read (CMD18):
  - stop transmission: STOP_TRAN = CMD12

  state transitions:
  ok -> read>ok
  ok -> (0xfe)write>ok
  ok -> (0xfe)write>writewait -> ok

  state transitions:
  ok -> readm(0xfe) -> readm(0xfe) -(STOP_TRAN)-> ok
  ok -> (0xfc)writem ->writemw -> (0xfc)writem -> writemw -(0xfd)-> ok

 */


enum mmcState {
    mmcNA = 0,
    mmcOk = 1,
    mmcWriteWait = 2, /* waiting for write to finish */
};
struct {
    enum mmcState state;
    s_int16 errors;
    s_int16 hcShift;
    u_int32 blocks;
#ifdef USE_MULTIPLE_BLOCK_WRITE
    u_int32 nextWriteBlock;
#endif
} mmc;


#ifdef USE_DEBUG
static char hex[] = "0123456789ABCDEF";
void puthex(u_int16 d) {
    register int i;
    char mem[5];
    for (i=0;i<4;i++) {
        mem[i] = hex[(d>>12)&15];
        d <<= 4;
    }
    mem[4] = '\0';
    fputs(mem, stdout);
}
#endif


extern struct FsNandPhys fsNandPhys;
extern struct FsPhysical *ph;
extern struct FsMapper *map;
extern struct Codec *cod;
extern struct CodecServices cs;
//extern u_int16 codecVorbis[];


s_int16 InitializeMmc(s_int16 tries) {
    register s_int16 i, cmd;
    mmc.state = mmcNA;
    mmc.blocks = 0;
    mmc.errors = 0;
#ifdef USE_MULTIPLE_BLOCK_WRITE
    mmc.nextWriteBlock = 0xffffffff;
#endif

#if DEBUG_LEVEL > 1
    puthex(clockX);
    puts(" clockX");
#endif
 tryagain:
    IdleHook();

    mmc.hcShift = 9;
    if (tries-- <= 0) {
        return ++mmc.errors;
    }
    for (i=0; i<512; i++) {
        SpiSendClocks();
    }

    /* MMC Init, command 0x40 should return 0x01 if all is ok. */
    i = MmcCommand(MMC_GO_IDLE_STATE/*CMD0*/|0x40,0);
    if (i != 1) {
#if DEBUG_LEVEL > 1
        puthex(i);
        puts(" Init");
#endif
        BusyWait10();
        goto tryagain;//continue; /* No valid idle response */
    }
    cmd = MMC_SEND_OP_COND|0x40;
#ifdef USE_HC
    /*CMD8 is mandatory before ACMD41 for hosts compliant to phys. spec. 2.00*/
    i = MmcCommand(MMC_SEND_IF_COND/*CMD8*/|0x40,
                   0x00000122/*2.7-3.6V*/); /*note: 0x22 for the right CRC!*/
#if DEBUG_LEVEL > 2
    puthex(i);
    puts("=IF_COND");
#endif
    if (i == 1) {
        /* MMC answers: 0x05 illegal command, v2.0 SD(HC-SD) answers: 0x01 */
        /* Should we read the whole R7 response? */
#if DEBUG_LEVEL > 1
        //SpiSendReceiveMmc(-1, 32);
        puthex(SpiSendReceiveMmc(-1, 16));
        puthex(SpiSendReceiveMmc(-1, 16));
        puts("=R7");
#else
        SpiSendReceiveMmc(-1, 32); /*read the whole response*/
#endif
        cmd = 0x40|41; /* ACMD41 - SD_SEND_OP_COND */
    }
#endif /*USE_HC*/


#if 0
    do {
        i = MmcCommand(MMC_READ_OCR/*CMD58*/|0x40, 0);
#if DEBUG_LEVEL > 1
        puthex(i);
        puthex(SpiSendReceiveMmc(-1, 16));
        puthex(SpiSendReceiveMmc(-1, 16));
        puts("=READ_OCR");
#endif
    } while (0);
#else
    /* Some cards require CMD58 after CMD8 -- does this still work with MMC?*/
    MmcCommand(MMC_READ_OCR/*CMD58*/|0x40, 0);
    SpiSendReceiveMmc(-1, 16);
    SpiSendReceiveMmc(-1, 16);
#endif

    /* MMC Wake-up call, set to Not Idle (mmc returns 0x00)*/
    i = 0; /* i is 1 when entered .. but does not make the code shorter*/
    while (1) {
        register int c;
#ifdef USE_HC
        if (cmd == (0x40|41)) {
            c = MmcCommand(0x40|55/*CMD55*/,0);
#if DEBUG_LEVEL > 2
            puthex(c);
            puts("=CMD55");
#endif
        }
#endif

        c = MmcCommand(cmd/*MMC_SEND_OP_COND|0x40*/,
#ifdef USE_HC
                       /* Support HC for SD, AND for MMC! */
                       //i ? 0x40100000UL : 0x00000000UL
                       0x40100000UL
#else
                       0
#endif
            );

#if DEBUG_LEVEL > 1
        puthex(c);
        puts("=ACMD41");
#endif

        if (c == 0) {
#if DEBUG_LEVEL > 1
            puts("got 0");
#endif
            break;
        }
        //BusyWait10();
        if (++i >= 25000 /*Large value (12000) required for MicroSD*/
            || c != 1) {
#if DEBUG_LEVEL > 1
            puthex(c);
            puts(" Timeout 2");
#endif
            goto tryagain; /* Not able to power up mmc */
        }
    }
#ifdef USE_HC
    i = MmcCommand(MMC_READ_OCR/*CMD58*/|0x40, 0);
#if DEBUG_LEVEL > 1
    puthex(i);
    puts("=READ_OCR");
#endif
    if (//cmd == (0x40|41) &&
        i == 0) {
        if (SpiSendReceiveMmc(-1, 16) & (1<<(30-16))) {
            /* OCR[30]:CCS - card capacity select */
            /* HighCapacity! */
#if DEBUG_LEVEL > 1
            puts("=HC");
#endif
            mmc.hcShift = 0;
        }
        /*read all of the OCR data to make some cards happy */
        SpiSendReceiveMmc(-1, 16);
    }
#endif /*USE_HC*/

#if DEBUG_LEVEL > 4
    if (MmcCommand(MMC_SEND_CID/*CMD10*/|0x40, 0) == 0) {
        register s_int16 *p = minifatBuffer;
        register int t = 3200;
        while (SpiSendReceiveMmc(0xff00, 8) == 0xff) {
            if (t-- == 0)
                goto tryagain;
        }
        for (i=0; i<8; i++) {
            *p++ = SpiSendReceiveMmc(-1, 16);
#if DEBUG_LEVEL > 1
            puthex(p[-1]);
#endif
/*
       Man     Productname   serial#   date
          App             rev        res    crc7+stop
  4G:  1D 4144 0000000000 00 0000157A 0 06A E3
  64M: 02 0000 53444D3036 34 40185439 0 C77 75
       00 0011 1122223333 44 44555566 6 677 77
*/
        }
#if DEBUG_LEVEL > 1
        puts("=CID");
#endif
    }
#endif

#if 0
    /* Set Block Size of 512 bytes -- default for at least HC */
    /* Needed by MaxNova S043618ATA 2J310700 MV016Q-MMC */
    /* Must check return value! (MicroSD restart!) */
    {
        register int c;
        if ((c = MmcCommand(MMC_SET_BLOCKLEN|0x40, 512)) != 0) {
#if DEBUG_LEVEL > 1
            puthex(c);
            puts(" SetBlockLen failed");
#endif
            goto tryagain;
        }
    }
#endif
    /* All OK return */
    //mmc.errors = 0;
    mmc.state = mmcOk;
    return 0;//mmc.errors;
}

struct FsMapper *FsMapMmcCreate(struct FsPhysical *physical,
                                u_int16 cacheSize);
u_int16 FsMapMmcRead(struct FsMapper *map, u_int32 firstBlock,
                     u_int16 blocks, u_int16 *data);
u_int16 FsMapMmcWrite(struct FsMapper *map, u_int32 firstBlock,
                      u_int16 blocks, u_int16 *data);

auto u_int16 MyReadDiskSector(register __i0 u_int16 *buffer,
                              register __reg_a u_int32 sector) {
    register s_int16 i;
    register u_int16 t = 65535;

    if (mmc.state == mmcNA || mmc.errors) {
        cs.cancel = 1;
        return 5;
    }
#ifdef USE_MULTIPLE_BLOCK_WRITE
    /* with "if" it is a little faster, but more code */
    //if (mmc.nextWriteBlock != 0xffffffff)
    {
        /* Finalize write mode */
        FsMapMmcWrite(NULL, 0, 0, NULL);
    }
#endif

#if 0 && defined(USE_DEBUG)
    puthex(sector>>16);
    puthex(sector);
    puts("=ReadDiskSector");
#endif
#ifdef USE_HC
    MmcCommand(MMC_READ_SINGLE_BLOCK|0x40,(sector<<mmc.hcShift));
#else
    MmcCommand(MMC_READ_SINGLE_BLOCK|0x40,(sector<<9));
#endif
    do {
        i = SpiSendReceiveMmc(0xff00,8);
    } while (i == 0xff && --t != 0);

    if (i != 0xfe) {
        memset(buffer, 0, 256);
        if (i > 15 /*unknown error code*/) {
            mmc.errors++;
        } else {
            /* data retrieval failed:
               D4-D7 = 0
               D3 = out of range
               D2 = Card ECC Failed
               D1 = CC Error
               D0 = Error
             */
        }
        SpiSendClocks();
        return 1;
    }
    for (i=0; i<512/2; i++) {
        *buffer++ = SpiSendReceiveMmc(0xffff,16);
#if 0
        puthex(buffer[-1]);
        if ((i & 15) == 15)
            puts("");
#endif
    }
    SpiSendReceiveMmc(0xffff,16); /* discard crc */

    /* generate some extra SPI clock edges to finish up the command */
    SpiSendClocks();
    SpiSendClocks();

    /* We force a call of user interface after each block even if we
       have no idle CPU. This prevents problems with key response in
       fast play mode. */
    IdleHook();
    return 0; /* All OK return */
}

#ifdef USE_MULTIPLE_BLOCK_WRITE
s_int16 FsMapMmcFlush(struct FsMapper *map, u_int16 hard) {
    /* Finalize write mode */
    FsMapMmcWrite(NULL, 0, 0, NULL);
    return 0;
}
#endif

#if 0
struct FsMapper *FsMapMmcCreate(struct FsPhysical *physical,
                                u_int16 cacheSize) {
    PERIP(GPIO0_MODE) &= ~(MMC_MISO|MMC_CLK|MMC_MOSI|MMC_XCS);
    PERIP(GPIO0_DDR) = (PERIP(GPIO0_DDR) & ~(MMC_MISO))
        | (MMC_CLK|MMC_MOSI|MMC_XCS)
        | GPIO0_SD_POWER;
    PERIP(GPIO0_ODATA) = (PERIP(GPIO0_ODATA) & ~(MMC_CLK|MMC_MOSI))
        | (MMC_XCS | GPIO0_CS1); /* NFCE high */
#if DEBUG_LEVEL > 1
    puts("Configured MMC pins\n");
#endif
    memset(&mmc, 0, sizeof(mmc));
    return &mmcMapper;
}
#endif




void CheckSd(void) {
#if 0
    if ((PERIP(GPIO0_IDATA) & (1<<14))) {
        /* SD not inserted */
        putstrp("\p!SD\n");

        /* If SD is not inserted, run SPI FLASH player. */
        //SetHookFunction((u_int16)ReadDiskSector, MapperReadDiskSector);
        PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE1;
        ResetIRAMVectors();
        SpiLoad(SPIPLAYERSTART+4, 1/*24-bit address*/);
    }
#endif
}
#ifdef TEST_WRITE
u_int32 dirSector;
u_int16 *dirEntry = NULL, *dirName = NULL;
#endif
void IterateFilesCallback(register __b0 u_int16 *name) {
    register int i;
#ifdef TEST_WRITE
    /* If dirName is not NULL,
       capture some data required to update the file entry.
       Otherwise perform the normal operation.
     */
    if (dirName) {
        if (!memcmp(dirName, name, 4)) { /* 8 characters match */
            dirSector = minifatInfo.currentSector;
            dirEntry = name; /*points to minifatBuffer*/
        }
        return;
    }
#endif /*TEST_WRITE*/

    /* Dump the whole FAT directory entry */
    for (i=0;i<32/2;i++) {
        putword(*name++);
    }
#ifdef SHOW_LONG_FILENAMES
    if (minifatInfo.longFileName[0]) {
        register __y s_int16 *p = minifatInfo.longFileName;
        while (*p >> 8) {
            putch(*p >> 8);
            if (*p & 255) {
                putch(*p & 255);
            } else {
                break;
            }
            p++;
        }
    }
    putch('\n');
#endif
}

#ifdef TEST_WRITE
/* This is a slightly edited version from sdmass.c -
   some features have been removed. */
u_int16 FsMapMmcWrite(struct FsMapper *map, u_int32 firstBlock,
                      u_int16 blocks, u_int16 *data) {
    /* We know this is called one block at a time by the USB ROM firmware,
       or our own code, so only prepare for that and save 14 words. */
    {
        register u_int16 c;
        if (mmc.state == mmcNA || mmc.errors)
            return 0;
#ifdef USE_HC
        c = MmcCommand(MMC_WRITE_BLOCK|0x40, firstBlock<<mmc.hcShift);
#else
        c = MmcCommand(MMC_WRITE_BLOCK|0x40, firstBlock<<9);
#endif
        //wait for BUSY token, if you get 0x01(idle), it's an ERROR!
        while (c != 0x00) {
            if (c == 0x01) {
                mmc.errors++;
                goto out;
            }
            c = SpiSendReceiveMmc(0xff00, 8);
            /*TODO: timeout?*/
        }
        SpiSendReceiveMmc(0xfffe, 16);
        for (c=0; c<256; c++) {
            SpiSendReceiveMmc(*data++, 16);
        }
        SpiSendReceiveMmc(0xffff, 16); /* send dummy CRC */
        /* wait until MMC write finished */
        do {
            /*TODO: timeout?*/
        } while ( SpiSendReceiveMmc(0xff00U, 8) != 0x00ffU);
    }
 out:
    SpiSendClocks();
    return 1;
}

static s_int16 fatBufDirty = 0; /* non-zero if minifatBuffer has been modified */

auto u_int16 FatOpenFileWrite(u_int16 *name, u_int32 suffix) {
    u_int32 ext[2];
    ext[0] = suffix;
    ext[1] = 0;
    minifatInfo.supportedSuffixes = ext;
    dirName = name;
    dirEntry = 0;
    IterateFiles();
    /* Clear dirName, so that IterateFiles works normally again.*/
    dirName = NULL;
    /* Restore the default suffixes. */
    minifatInfo.supportedSuffixes = supportedFiles;

    if (!dirEntry) {
        /* The entry was not found. */
        return 0;
    }
    /* Open to get the fragments and other data correct.
       We already know that it should exist.*/
    return OpenFileNamed(name, suffix) != 0xffffU;
}


/* Analogous to FatReadFile. The data is packed bytes, byteOff can be used
   to start at any byte inside the word array. */
auto s_int16 FatWriteFile(register __i3 u_int16 *buf,
        		  register __c1 s_int16 byteOff,
        		  register __c0 s_int16 byteSize) {
    __mem_y s_int16 srcIdx = byteOff;
    s_int16 writeAmount;
    while (byteSize) {
        writeAmount = byteSize;
        /* Check for sector boundary. Split into two writes if needed. */
        if ((minifatInfo.filePos >> 9) <
            ((minifatInfo.filePos + writeAmount - 1) >> 9)) {
            writeAmount = 512 - ((u_int16)minifatInfo.filePos & 511);
        }
        /* Prevent writing off the end of the allocated space of a file.
           This requires that the file size will be fixed in FatCloseFile.
        */
        if (minifatInfo.filePos + writeAmount >
            512*minifatFragments[0].size) {
            writeAmount = minifatFragments[0].size*512-minifatInfo.filePos;
            if (writeAmount <= 0) {
        	return srcIdx - byteOff; /*EOF*/
            }
        }

        {
            /*For files shorter than 560MB we don't need PatchFatFindSector()*/
            u_int32 sect = FatFindSector(minifatInfo.filePos);
            if (fatBufDirty) {
        	/*Write a changed sector back to SD before reading over it. */
        	fatBufDirty = 0;
        	FsMapMmcWrite(map, minifatInfo.currentSector, 1,minifatBuffer);
            }
            if (minifatInfo.currentSector != sect) {
        	ReadDiskSector(minifatBuffer, minifatInfo.currentSector= sect);
            }
        }
        MemCopyPackedBigEndian(minifatBuffer,
        		       minifatInfo.filePos & 511,
        		       buf, srcIdx, writeAmount);
        fatBufDirty = 1;
        srcIdx += writeAmount;
        minifatInfo.filePos += writeAmount;
        if (minifatInfo.fileSize < minifatInfo.filePos) {
            minifatInfo.fileSize = minifatInfo.filePos;
        }
        byteSize -= writeAmount;
    }
    return writeAmount;
}
auto s_int16 FatCloseFile(void) {
    if (fatBufDirty) {
        /* Write a modified buffer back to SD*/
        fatBufDirty = 0;
        FsMapMmcWrite(map, minifatInfo.currentSector, 1, minifatBuffer);
    }
    if (dirEntry) {    /* Update file size if possible */
 	ReadDiskSector(minifatBuffer, minifatInfo.currentSector = dirSector);
        /* bytes 28,29,30,31 in the directory entry are the file size
           in little-endian format. */
        dirEntry[28/2] = SwapWord(minifatInfo.fileSize);
        dirEntry[30/2] = SwapWord(minifatInfo.fileSize>>16);
        FsMapMmcWrite(map, minifatInfo.currentSector, 1, minifatBuffer);
    }
    dirEntry = 0; /* We're done with the file */
}
#endif/*TEST_WRITE*/


void main(void) {
#if defined(GPIO_CONTROL) && defined(GPIO_NUMBERED_FILES)
    u_int16 gpNum;
    u_int16 gpName[4] = "\pAUDIO00 ";
#endif
    Enable();

#ifdef GAPLESS
    codecVorbis.audioBegins = 0;
#endif
#if 1
    InitAudio(); /* goto 3.0x..4.0x */
    PERIP(INT_ENABLEL) = INTF_RX | INTF_TIM0;
    PERIP(INT_ENABLEH) = INTF_DAC;
#endif


#ifdef USE_WAV
    /* allow both .WAV and .OGG */
    supportedFiles = defSupportedFiles;
#endif

#if 1
    SetHookFunction((u_int16)OpenFile, FatFastOpenFile); /*Faster+fat12!*/
    //SetHookFunction((u_int16)OpenFile, FatFastOpenFileD); /*Faster!*/
#else
#if !defined(USE_WAV) || !defined(USE_USB)
    /* Ignore subdirectories in FAT12 disks */
    SetHookFunction((u_int16)OpenFile, Fat12OpenFile);
#endif
#endif
    //voltages[voltCorePlayer] = voltages[voltCoreUSB] = 29;//27;
    //voltages[voltIoUSB] = voltages[voltIoPlayer] = 27; /* 3.3V */
    //voltages[voltAnaPlayer] = 30; /*3.6V for high-current MMC/SD!*/
    //PowerSetVoltages(&voltages[voltCorePlayer]);

#ifdef UART_CONTROL
    SetHookFunction((u_int16)IdleHook, NullHook);     /* no default keyscan */
    SetHookFunction((u_int16)USBSuspend, NullHook);   /* no low-power state */
    SetHookFunction((u_int16)LoadCheck, UartLoadCheck); /* fixed clock */
#endif
#ifdef GPIO_CONTROL
  SetHookFunction((u_int16)IdleHook, GPIOCtrlIdleHook);
#endif


    /* Replicate main loop */
//    player.volume = 17;
//    player.volumeOffset = -24;
    player.pauseOn = 0;
    //player.randomOn = 0;
//    keyOld = KEY_POWER;
//    keyOldTime = -32767; /* ignores the first release of KEY_POWER */

    /*
      MMC mapper replaces FLASH mapper, so we do not need to hook
      ReadDiskSector.

      We must also replace the MassStorage() functionality, because it
      automatically deletes mapper, creates a read-write FLASH mapper,
      and restores read-only FLASH mapper at exit.

      We need to replace the main loop because we want to detect
      MMC/SD insertion and removal.

      We do not hook the MassStorage, because we can call our own version
      from the main loop without using the RAM hook. This saves some
      instruction words.
     */
#if 0
    map = FsMapMmcCreate(NULL, 0); /* Create MMC Mapper */
#else
    /* inlined FsMapMmcCreate */
    PERIP(GPIO0_MODE) &= ~(MMC_MISO|MMC_CLK|MMC_MOSI|MMC_XCS);
    PERIP(GPIO0_DDR) = (PERIP(GPIO0_DDR) & ~(MMC_MISO))
        | (MMC_CLK|MMC_MOSI|MMC_XCS)
        | GPIO0_SD_POWER;
    //PERIP(GPIO0_ODATA) = (PERIP(GPIO0_ODATA) & ~(MMC_CLK|MMC_MOSI))
    //| (MMC_XCS | GPIO0_CS1); /* NFCE high */
    PERIP(GPIO0_SET_MASK)   = (MMC_XCS | GPIO0_CS1); /* NFCE high */
    PERIP(GPIO0_CLEAR_MASK) = (MMC_CLK|MMC_MOSI);
    memset(&mmc, 0, sizeof(mmc));
    SetHookFunction((u_int16)ReadDiskSector, MyReadDiskSector);
#endif


//    player.volume = -24; /* "max" volume */
    //PlayerVolume();


#ifdef UART_CONTROL
    UartInit(); //Set interrupt etc.
#endif /*UART_CONTROL*/
#ifdef GPIO_CONTROL
    GPIOInit();
#endif /*GPIO_CONTROL*/

    while (1) {
        CheckSd();

        /* If MMC is not available, try to reinitialize it. */
        if (mmc.state == mmcNA || mmc.errors) {
            /* enable SD power */
            PERIP(GPIO0_SET_MASK) = GPIO0_SD_POWER;
            InitializeMmc(50);
        }

#ifdef USE_USB
        /* If USB is attached, call our version of the MassStorage()
           handling function. */
        if (USBIsAttached()) {
            PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE1;
            ResetIRAMVectors();
            if (mmc.state == mmcOk && mmc.errors == 0) {
        	SpiLoad(SDMASSSTART+4, 1/*24-bit address*/);
            } else {
        	SpiLoad(0+4, 1/*24-bit address*/); //spi flash mass-storage
            }
        }
#endif /*USE_USB*/

        /* Try to init FAT. */
        if (InitFileSystem() == 0) {
            /*
              Check if there is an update file (SDUPDATE.PRG) on the SD card.
            */
#ifdef TEST_WRITE
            static int didit = 0;
            if (!didit) {
        	/*
        	   To be able to update the file size, we use
        	   IterateFiles() / IterateFilesCallback() and some internal
        	   structures to locate the file entry to be able to update
        	   it later.

        	   This code with the changes in IterateFilesCallback()
        	   finds the directory sector and entry this file resides
        	   in, so we can optionally fix the file size later. */
        	if (FatOpenFileWrite("\pCONFIG  ", FAT_MKID('T','X','T'))) {

        	    Seek(24); /* skip a bit for testing */
        	    while (FatWriteFile("\palternative \n\r", 0, 14)) {
        		/* Fill the rest of the file with this */
        	    }
        	    FatCloseFile(); /* Flush the last modified sector */

        	    putstrp("\preplaced text in config.txt\n");
        	    putword(minifatInfo.filePos>>16);
        	    putword(minifatInfo.filePos);
        	    putch('\n');
        	}
        	didit = 1; /* do only once */
            }
#else/*TEST_WRITE*/
#if 1//def USE_WAV
            static const u_int32 allowedExtensions[] = {
                FAT_MKID('P','R','G'), 0
            };
            static const __y u_int16 sdupdate[] = "\pSDUPDATE";
            minifatInfo.supportedSuffixes = allowedExtensions;
            if (OpenFile(0) < 0
                && !memcmpY(minifatInfo.fileName, sdupdate, 8/2)) {
                /* Note:
                   The maximum size for the program image is 8192 bytes.
                */
                if (ReadFile(mallocAreaX, 0, 0x2000)) {
                    Disable();
                    BootFromX(mallocAreaX+8); // Exit to new image
                    /* BootFromX returns only in case of invalid prg file.
                       Program execution would be unsafe after failed
                       BootFromX so restart using watchdog reset. */
                    USEX(WDOG_CONFIG) = 2;
                    USEX(WDOG_RESET) = 0x4ea9; /*Activate watchdog*/
                    while (1)
                        ;
                }
            }
#else
            /* If OpenFileNamed() is used anyway, use this version */
            if (OpenFileNamed("\pSDUPDATE", FAT_MKID('P','R','G'))!=0xffffU) {
                /* Note:
                   The maximum size for the program image is 8192 bytes.
                */
                if (ReadFile(mallocAreaX, 0, 0x2000)) {
                    Disable();
                    BootFromX(mallocAreaX+8); // Exit to new image
                    /* BootFromX returns only in case of invalid prg file.
                       Program execution would be unsafe after failed
                       BootFromX so restart using watchdog reset. */
#ifndef USE_WAV
                    USEX(WDOG_CONFIG) = 2;
                    USEX(WDOG_RESET) = 0x4ea9; /*Activate watchdog*/
#endif
                    while (1)
                        ;
                }
            }
#endif
#endif/*elseTEST_WRITE*/

#ifdef UART_CONTROL
            putstrp("\pfat");
            putword(minifatInfo.totSize>>16);
            putword(minifatInfo.totSize);
            putch('\n');
#endif
            /* Restore the default suffixes. */
            minifatInfo.supportedSuffixes = supportedFiles;
            player.totalFiles = OpenFile(0xffffU);
#ifdef UART_CONTROL
            putstrp("\pfiles");
            putword(player.totalFiles);
            putch('\n');
#endif
            if (player.totalFiles == 0) {
                /* If no files found, output some samples.
                   This causes the idle hook to be called, which in turn
                   scans the keys and you can turn off the unit if you like.
                */
#ifdef USE_DEBUG
                puts("no .ogg");
#endif
                goto noFSnorFiles;
            }

            /*
              Currently starts at the first file after a card has
              been inserted.

              Possible improvement would be to save play position
              to SPI EEPROM for a couple of the last-seen MMC/SD's.
              Could also save the play mode, volume, and earspeaker
              state to EEPROM.
            */
#if !defined(GPIO_CONTROL)
            player.nextStep = 1;
            player.nextFile = 0;
#endif
            while (1) {
                CheckSd();

#ifdef UART_CONTROL
                if (playerMode == PLAYER_WAIT_FOR_COMMAND
#ifdef USE_QUEUE
                    && 0 == queued
#endif
                    ) {
                    static u_int16 buffer[80];
                    register u_int16 *bf = buffer;

                    while (1) {

                        player.currentFile = -1;
                        if (UartFill()) {
                            int cmd = UartGetByte();
                            *bf = 0;
                            if (cmd == '\n' || bf == &buffer[79]) {
                                u_int16 fName[10/2];
                                __y u_int32 fSuffix =
                                    FAT_MKID(buffer[9],buffer[10],buffer[11]);
                                /* Build a 8-character filename */
                                fName[0] = (buffer[1]<<8) | buffer[2];
                                fName[1] = (buffer[3]<<8) | buffer[4];
                                fName[2] = (buffer[5]<<8) | buffer[6];
                                fName[3] = (buffer[7]<<8) | buffer[8];
                                fName[4] = ('\n'<<8) | 0;

                                if (!memcmp(buffer, "OFF", 3)) {
                                    putch('o');
                                    RealPowerOff();
                                }
                                switch (buffer[0]) {
                                case 'c':
                                    /* continuous play mode */
                                    playerMode = PLAYER_CONTINUOUS_PLAY;
                                    goto playloop;

                                case 'L':
                                    /* List files */
#if 0
                                    /*Set suffix to NULL to list all files
        			      TODO: use a parameter?
        			     */
                                    //minifatInfo.supportedSuffixes = NULL;
#endif
#ifdef SHOW_LONG_FILENAMES
                                    minifatInfo.longFileName[0] = 0;
#endif
                                    IterateFiles();
                                    break;

#ifdef USE_QUEUE
                            case 'Q':
                                if (queued < QUEUE_SIZE) {
                                    u_int16 num = OPENFILENAMED(fName, fSuffix);
                                    if (num != 0xffffU) {
                                        queue[queued++] = num;
                                        putstrp("\padd ");
                                    } else {
                                        putstrp("\pnof ");
                                    }
                                    putstrp(fName);
                                } else {
                                    putstrp("\pful\n");
                                }
                                break;
#endif
#ifdef USE_TYPE
                                case 'T':
                                { /* Type by name */
                                    u_int16 num = OpenFileNamed(fName, fSuffix);
                                    if (num != 0xffffU) {
                                        int l;
                                        putword(num);

                                        while (l = ReadFile(buffer, 0,
                                                            2*sizeof(buffer))) {
                                            register int i;
                                            for (i=0; i<l/2; i++) {
                                                putword(buffer[i]);
                                            }
                                            if (l & 1) {
                                                putch(buffer[l/2]>>8);
                                            }
                                        }
                                    } else {
                                        putword(-1);
                                    }
                                    break;
                                }
#endif
#ifdef USE_INFO
                                case 'I':  /* 'I'nfo by name */
#endif
                                case 'P':  /* 'P'lay by name */
                                    player.nextFile =
                                        OPENFILENAMED(fName, fSuffix);
                                    goto checkvalidfile;
#ifdef USE_INFO
                                case 'i': /* 'i'nfo by number */
#endif
                                case 'p': /* 'p'lay by number */
                                    player.nextFile = atou((void *)&buffer[1]);
                                checkvalidfile:
                                    if (player.nextFile >= player.totalFiles
        				|| player.nextFile < 0) {
        				putstrp("\pnot found\n");
        				break;
        			    }
#ifdef USE_INFO
                                    if (buffer[0] == 'i' || buffer[0] == 'I') {
                                        u_int32 secs;
        				OpenFile(player.nextFile);
        				secs = PlayTimeFile(0);
        				putword(secs>>16);
        				putword(secs);
        				putstrp("\pseconds\n");
        				break;
                                    }
#endif
        			    goto playloop;
                                }
                                bf = buffer;
                            } else {
                                *bf++ = cmd;
                            }
                        }
                        memset(tmpBuf, 0, sizeof(tmpBuf)); /* silence */
                        AudioOutputSamples(tmpBuf, sizeof(tmpBuf)/2); /* no pause */
                        //cs.Output(&cs, tmpBuf, sizeof(tmpBuf)/2);
                        CheckSd();
                    }
                }
#else /*elseUART_CONTROL*/
                PERIP(GPIO0_MODE) &= ~(VLSI_RDY_LED);
                PERIP(GPIO0_DDR) |= (VLSI_RDY_LED);
                PERIP(GPIO0_ODATA) |= (VLSI_RDY_LED);
                if (player.nextFile == -1) {
                    player.currentFile = -1;
                    while ( player.currentFile == -1 ) {
                        memset(tmpBuf, 0, sizeof(tmpBuf)); /* silence */
                        AudioOutputSamples(tmpBuf, sizeof(tmpBuf)/2); /*no pause*/
                        //cs.Output(&cs, tmpBuf, sizeof(tmpBuf)/2);

                        CheckSd();
                        IdleHook();
#ifdef USE_USB
                        if (USBIsAttached()) { /* .. or USB is attached. */
                            break;
                        }
#endif
                    }
                }
#endif /*elseUART_CONTROL*/

            playloop:


#if 0 /*disable random play*/
                /* If random play is active, get the next random file,
                   but try to avoid playing the same song twice in a row. */
                if (player.randomOn) {
                    register s_int16 nxt;
                retoss:
                    nxt = rand() % player.totalFiles;
                    if (nxt == player.currentFile && player.totalFiles > 1)
                        goto retoss;
                    player.currentFile = nxt;
                } else
#endif
                {
                    player.currentFile = player.nextFile;
                }
                if (player.currentFile < 0) {
                    player.currentFile += player.totalFiles;
                }
                if (player.currentFile >= player.totalFiles) {
                    player.currentFile -= player.totalFiles;
                }
#if defined(GPIO_CONTROL)
                player.nextFile = -1;
#else
                player.nextFile = player.currentFile + 1;
#endif
                /* If the file can be opened, start playing it. */
#if defined(GPIO_CONTROL) && defined(GPIO_NUMBERED_FILES)
                /* numbered files AUDIO01.OGG .. AUDIOXX.OGG */
                gpName[2] = (('O'<<8) | '0') + (player.currentFile / 10);
                gpName[3] = ((player.currentFile % 10) << 8) + (('0'<<8) | ' ');
                gpNum = OPENFILENAMED(gpName, FAT_MKID('O','G','G'));
                if (gpNum != 0xffffU)
#else
#ifdef USE_QUEUE
                if (queued) {
                    putstrp("\pqueue ");
                    putch(queued/10+'0');
                    putch(queued%10+'0');
                    putch('\n');
                    queued--;
                    player.currentFile = queue[0];
                    memmove(&queue[0], &queue[1], QUEUE_SIZE-1);
                }
#endif
                if (OpenFile(player.currentFile) < 0)
#endif
                {
                    //player.volumeOffset = -12; /*default volume offset*/
                    //PlayerVolume();
                    player.ffCount = 0;
                    cs.cancel = 0;
                    cs.goTo = -1; /* start playing from the start */
                    cs.fileSize = cs.fileLeft = minifatInfo.fileSize;
                    cs.fastForward = 1; /* reset play speed to normal */
#ifdef UART_CONTROL
#ifdef GAPLESS
                    putch(player.currentFile); /*minimum delay*/
#else /*GAPLESS*/
                    putstrp("\pplay");
                    putword(player.currentFile);

                    putword(minifatInfo.fileName[0]);
                    putword(minifatInfo.fileName[1]);
                    putword(minifatInfo.fileName[2]);
                    putword(minifatInfo.fileName[3]);
                    putword(minifatInfo.fileName[4]);
                    putch(minifatInfo.fileName[5]>>8);
                    putch('\n');
#endif/*elseGAPLESS*/

                    SetHookFunction((u_int16)IdleHook, UartIdleHook);
#endif/*UART_CONTROL*/

                    PlayerVolume(); /*required for QUIET_CANCEL*/
#ifdef USE_WAV
                    PlayWavOrOggFile();
#else
#ifdef GAPLESS
        	    PlayGaplessOggFile();
#else
                    PatchPlayCurrentFile();
#endif
#endif
#ifdef UART_CONTROL
#ifdef GAPLESS
                    putch('d');
#else /*GAPLESS*/
                    putstrp("\pdone\n");
#endif/*elseGAPLESS*/
                    SetHookFunction((u_int16)IdleHook, NullHook);
#endif/*UART_CONTROL*/
                } else {
                    player.nextFile = 0;
                }
                /* Leaves play loop when MMC changed */
                if (mmc.state == mmcNA || mmc.errors) {
                    break;
                }
#ifdef USE_USB
                if (USBIsAttached()) { /* .. or USB is attached. */
                    break;
                }
#endif
            }
        } else {
            /* If not a valid FAT (perhaps because MMC/SD is not inserted),
               just send some samples to audio buffer and try again. */
#ifdef UART_CONTROL
            putstrp("\pnofat\n");
#endif
        noFSnorFiles:
#ifdef GAPLESS
            codecVorbis.audioBegins = 0;
#endif
            LoadCheck(&cs, 32); /* decrease or increase clock */
            memset(tmpBuf, 0, sizeof(tmpBuf));
            AudioOutputSamples(tmpBuf, sizeof(tmpBuf)/2);
            /* When no samples fit, calls the user interface
               -- handles volume control and power-off. */
        }
    }
}
