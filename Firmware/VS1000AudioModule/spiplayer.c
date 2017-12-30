/// \file spiplayer.c Low-power SPI FLASH player
// derived from: spiusb.c Winbond 25X16 SPI flash/USB/Vorbis player
/*
  Copyright 2008-2013 VLSI Solution Oy. Absolutely no warranty.
*/  

#include "system.h"

#define USE_USB /* You get a bit more free code space if you disable USB. */
#ifdef DISABLE_USB
#undef USE_USB
#endif

#define POFFTIME 60 /*5 minutes*/

/*Turn analog off in low-power pause mode to save some power. */
//#define PATCH_LOW_POWER_MODE

#define SPI_CLOCK_DIVIDER 3 //2 is ok for 3.0x, 3 for 3.5x

// Command set of the SPI flash
#define SPI_EEPROM_COMMAND_WRITE_ENABLE  0x06
#define SPI_EEPROM_COMMAND_WRITE_DISABLE  0x04
#define SPI_EEPROM_COMMAND_READ_STATUS_REGISTER  0x05
#define SPI_EEPROM_COMMAND_WRITE_STATUS_REGISTER  0x01
#define SPI_EEPROM_COMMAND_READ  0x03
#define SPI_EEPROM_COMMAND_WRITE 0x02
#define SPI_EEPROM_COMMAND_CLEAR_ERROR_FLAGS 0x30
#define SPI_EEPROM_COMMAND_ERASE_4K_SECTOR 0x20
#define SPI_EEPROM_COMMAND_ERASE_CHIP 0xC7


// Position of workspace in Y ram, do not change.
#define WORKSPACE (mallocAreaY + 6144)


#include <stdio.h> //Standard io
#include <stdlib.h> // VS_DSP Standard Library
#include <vs1000.h> // VS1000B register definitions
#include <vectors.h> // VS1000B vectors (interrupts and services)
#include <minifat.h> // Read Only Fat Filesystem
#include <mapper.h> // Logical Disk
#include <string.h> // memcpy etc
#include <player.h> // VS1000B default ROM player
#include <audio.h> // DAC output
#include <codec.h> // CODEC interface
#include <vsNand.h>
#include <mappertiny.h>
#include <usb.h>

#include <dev1000.h>

#ifdef UART_CONTROL
#include "uartctrl.h"
#endif /*UART_CONTROL*/
#ifdef GPIO_CONTROL
#include "gpioctrl.h"
#endif /*UART_CONTROL*/


#ifdef USE_QUEUE
#define QUEUE_SIZE 100
s_int16 queued = 0;
u_int16 queue[QUEUE_SIZE];
#endif/*USE_QUEUE*/


extern struct FsNandPhys fsNandPhys;
extern struct FsPhysical *ph;
extern struct FsMapper *map;
extern struct Codec *cod;
extern struct CodecServices cs;
//extern u_int16 codecVorbis[];

//macro to set SPI to MASTER; 8BIT; FSYNC Idle => xCS high
#define SPI_MASTER_8BIT_CSHI   PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE1

//macro to set SPI to MASTER; 8BIT; FSYNC not Idle => xCS low
#define SPI_MASTER_8BIT_CSLO   PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE0

//macro to set SPI to MASTER; 16BIT; FSYNC not Idle => xCS low
#define SPI_MASTER_16BIT_CSLO  PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN16 | SPI_CF_FSIDLE0


/// Wait for not_busy (status[0] = 0) and return status
u_int16 SpiWaitStatus(void) {
  u_int16 status;
  SPI_MASTER_8BIT_CSHI;
  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive(SPI_EEPROM_COMMAND_READ_STATUS_REGISTER);
  do {
    status = SpiSendReceive(0);
    if (PERIP(USB_STATUS) & USB_STF_BUS_RESET){
      USBHandler();
      SPI_MASTER_8BIT_CSHI;
      return (u_int16)-1; /* USB HAS BEEN RESET */
    }
  } while (status & 0x01);
    ; //Wait until chip is ready or return -1 if USB bus reset

  SPI_MASTER_8BIT_CSHI;
  
  return status;
}


// Block Read for SPI EEPROMS with 24-bit address e.g. up to 16MB 
u_int16 EeReadBlock(u_int16 blockn, u_int16 *dptr) {
  SpiWaitStatus();
  
  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive(SPI_EEPROM_COMMAND_READ);
  SpiSendReceive(blockn>>7);            // Address[23:16] = blockn[14:7]
  SpiSendReceive((blockn<<1)&0xff);     // Address[15:8]  = blockn[6:0]0
  SpiSendReceive(0);                    // Address[7:0]   = 00000000
  SPI_MASTER_16BIT_CSLO;

  {
    int n;
    for (n=0; n<256; n++){
#if USE_INVERTED_DISK_DATA
      *dptr++ = ~SpiSendReceive(0);
#else
      *dptr++ = SpiSendReceive(0);
#endif
    }
  }
  SPI_MASTER_8BIT_CSHI;
  return 0;
}


struct FsMapper *FsMapSpiFlashCreate(struct FsPhysical *physical,
                                u_int16 cacheSize);
s_int16 FsMapSpiFlashRead(struct FsMapper *map, u_int32 firstBlock,
                     u_int16 blocks, u_int16 *data);

const struct FsMapper spiFlashMapper = {
    0x010c, /*version*/
    256,    /*blocksize*/
    LOGICAL_DISK_BLOCKS,      /*blocks*/
    0,      /*cacheBlocks*/
    FsMapSpiFlashCreate,
    FsMapFlNullOk,//RamMapperDelete,
    FsMapSpiFlashRead,
    FsMapFlNullOk,//FsMapSpiFlashWrite,
    NULL,//FsMapFlNullOk,//RamMapperFree,
    FsMapFlNullOk,//FsMapSpiFlashFlush,//RamMapperFlush,
    NULL /* no physical */
};
struct FsMapper *FsMapSpiFlashCreate(struct FsPhysical *physical,
                                     u_int16 cacheSize) {

  SPI_MASTER_8BIT_CSHI;
  PERIP(SPI0_FSYNC) = 0;
  PERIP(SPI0_CLKCONFIG) = SPI_CC_CLKDIV * (SPI_CLOCK_DIVIDER-1);
  PERIP(GPIO1_MODE) |= 0x1f; /* enable SPI pins */

  return &spiFlashMapper;
}



s_int16 FsMapSpiFlashRead(struct FsMapper *map, u_int32 firstBlock,
                          u_int16 blocks, u_int16 *data) {
  register s_int16 bl = 0;

  firstBlock += RESERVED_BLOCKS;

  while (bl < blocks) {
    EeReadBlock(firstBlock, data);
    data += 256;
    firstBlock++;
    bl++;
  }
  return bl;
}

#ifdef PATCH_LOW_POWER_MODE
void MyUSBSuspend(u_int16 timeOut) {
  PERIP(SCI_STATUS) |=   SCISTF_ANA_PDOWN | SCISTF_ANADRV_PDOWN;
  RealUSBSuspend(timeOut);
  PERIP(SCI_STATUS) &= ~(SCISTF_ANA_PDOWN | SCISTF_ANADRV_PDOWN);
}
#endif


void CheckSd(void) {
    if ((PERIP(GPIO0_DDR) & (1<<14)) == 0 &&
        (PERIP(GPIO0_IDATA) & (1<<14))) {
        /* SD not inserted */
    } else {
        /* SD inserted */
        //putch('S');
        putch('S');
        putch('D');
        putch('\n');

        Disable();
        ResetIRAMVectors();
        PERIP(INT_ENABLEL) = INTF_RX | INTF_TIM0;
        PERIP(INT_ENABLEH) = INTF_DAC;

        /* If SD inserted, run SD player. */
        PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE1;
        SpiLoad(SDPLAYERSTART+4, 1/*24-bit address*/);
        while (1)
            ;
    }
}


void IterateFilesCallback(register __b0 u_int16 *name) {
    register int i;
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

void main(void) {
#if defined(GPIO_CONTROL) && defined(GPIO_NUMBERED_FILES)
    u_int16 gpNum;
    u_int16 gpName[4] = "\pAUDIO00 ";
#endif

#ifdef GAPLESS
    codecVorbis.audioBegins = 0;
#endif
  InitAudio();

  PERIP(INT_ENABLEL) |= INTF_RX | INTF_TIM0;
  PERIP(INT_ENABLEH) = INTF_DAC;
  
  PERIP(SCI_STATUS) &= ~SCISTF_USB_PULLUP_ENA;
  PERIP(USB_CONFIG) = 0x8000U;

#if 0
  PERIP(GPIO1_ODATA) |=  LED1|LED2;
  PERIP(GPIO1_DDR)   |=  LED1|LED2;
  PERIP(GPIO1_MODE)  &= ~(LED1|LED2);
#endif

  player.volumeOffset = 0;
  player.pauseOn = 0;
#ifdef POFFTIME
  player.offDelay = POFFTIME;
#endif

  keyOld = KEY_POWER;
  keyOldTime = -32767;

  SetHookFunction((u_int16)OpenFile, Fat12OpenFile);
#ifdef UART_CONTROL
  SetHookFunction((u_int16)IdleHook, NullHook);     /* no default keyscan */
  SetHookFunction((u_int16)USBSuspend, NullHook);   /* no low-power state */
  SetHookFunction((u_int16)LoadCheck, UartLoadCheck); /* fixed clock */
#endif /*UART_CONTROL*/
#ifdef GPIO_CONTROL
  SetHookFunction((u_int16)IdleHook, GPIOCtrlIdleHook);
#endif
 
#ifdef USE_WAV
  /* allow both .WAV and .OGG */
  supportedFiles = defSupportedFiles;
  /* Install the new file player function and return to player. */
  SetHookFunction((u_int16)PlayCurrentFile, PlayWavOrOggFile);
#else
#ifdef GAPLESS
  SetHookFunction((u_int16)PlayCurrentFile, PlayGaplessOggFile);
#else
  SetHookFunction((u_int16)PlayCurrentFile, PatchPlayCurrentFile);
#endif
#endif

  // Use our SPI flash mapper as logical disk
  // In player mode we would only need to replace ReadDiskSector()
  map = FsMapSpiFlashCreate(NULL, 0); 

#if 0
player.volume = -24;
PlayerVolume();
#endif

#if 1
  /* set cs.sampleRate to non-zero because we may use cs.Output() */
  //clockX = player.maxClock = 2; /* 1.0x */
  SetRate(cs.sampleRate = 8000U);

  /* These are for low-power pause mode, because USB mode is not entered */
  voltages[voltCoreSuspend] = 11; /*1.7V*/
  voltages[voltIoSuspend]   = 0;  /*1.7V*/
  voltages[voltAnaSuspend]  = 0;  /*2.4V*/

  /* If maxClock is restricted, can use lower voltages. */
  //voltages[voltCorePlayer] = 11; /*1.7V*/
  //voltages[voltIoPlayer]   = 0;  /*1.7V*/
  //voltages[voltAnaPlayer]  = 0;  /*2.4V*/
  //PowerSetVoltages(&voltages[voltCorePlayer]);
  //puts("power");

//  player.volume = 0;//+24;
//  PlayerVolume();
#endif
#ifdef PATCH_LOW_POWER_MODE
  SetHookFunction((u_int16)USBSuspend, MyUSBSuspend);
#endif
  
#ifdef UART_CONTROL
  UartInit(); // Redirect RX interrupt etc.
#endif /*UART_CONTROL*/
#ifdef GPIO_CONTROL
  GPIOInit(); // Configure GPIO etc.
#endif /*GPIO_CONTROL*/

  // Use custom main loop to take over total control of chip
  while (1) {
    //TODO: check if MMC/SD is inserted, otherwise
    CheckSd();

    // Try to use a FAT filesystem on logical disk
    if (InitFileSystem() == 0) {
#ifdef UART_CONTROL
        putch('f');
        putch('a');
        putch('t');
        putword(minifatInfo.totSize>>16);
        putword(minifatInfo.totSize);
        putch('\n');
#endif /*UART_CONTROL*/
        minifatInfo.supportedSuffixes = supportedFiles; //.ogg
      
        // Look for playable files
        player.totalFiles = OpenFile(0xffffU);
#ifdef UART_CONTROL
        putstrp("\pfiles");
        putword(player.totalFiles);
        putch('\n');
#endif /*UART_CONTROL*/
        if (player.totalFiles == 0) {
            goto noFSnorFiles;
        }
        // Playable file(s) found. Play.
#if !defined(GPIO_CONTROL)
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
                            fName[4] = ('\n'<<8)|0;

                            if (!memcmp(buffer, "OFF", 3)) {
                                putch('o');
                                RealPowerOff();
                            }
                            switch (buffer[0]) {
                            case 'c':
                                /* continuous play mode */
                                playerMode = PLAYER_CONTINUOUS_PLAY;
                                goto playloop;
#if 1
                            case 'L':
                                /* List files */
                                //minifatInfo.supportedSuffixes = NULL;
                                /*all files*/
                                minifatInfo.longFileName[0] = 0;
                                IterateFiles();
                                break;
#endif
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
#if 0
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
#ifdef USE_USB
                    if (USBIsAttached()) { /* .. or USB is attached. */
                        break;
                    }
#endif
                }
            }
#else /*elseUART_CONTROL*/
            if (player.nextFile == -1) {
                player.currentFile = -1;
                while ( player.currentFile == -1 ) {
                    
                    memset(tmpBuf, 0, sizeof(tmpBuf)); /* silence */
                    AudioOutputSamples(tmpBuf, sizeof(tmpBuf)/2); /*no pause*/
                    //cs.Output(&cs, tmpBuf, sizeof(tmpBuf)/2);
                    
                    CheckSd();
#ifdef USE_USB
                    if (USBIsAttached()) { /* .. or USB is attached. */
                        break;
                    }
#endif
                }
            }
#endif /*elseUART_CONTROL*/

    playloop:
            /* No random play */
            player.currentFile = player.nextFile;
            if (player.currentFile < 0) 
                player.currentFile += player.totalFiles;
            if (player.currentFile >= player.totalFiles)
                player.currentFile -= player.totalFiles;

#if defined(GPIO_CONTROL)
            player.nextFile = -1;
#else
            player.nextFile = player.currentFile + 1;
#endif
#ifndef UART_CONTROL
//putch(0x55);
//putch(player.currentFile);
#endif
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
            opened:
                player.ffCount = 0;
                cs.cancel = 0;
                cs.goTo = -1;
                cs.fileSize = cs.fileLeft = minifatInfo.fileSize;
                cs.fastForward = 1;

        	/* Note: printouts should not be used with GAPLESS */
#ifdef UART_CONTROL
#ifdef GAPLESS
        	putch(player.currentFile);
#else
                putstrp("\pplay");
                putword(player.currentFile);
                putword(minifatInfo.fileName[0]);
                putword(minifatInfo.fileName[1]);
                putword(minifatInfo.fileName[2]);
                putword(minifatInfo.fileName[3]);
                putword(minifatInfo.fileName[4]);
                putch(minifatInfo.fileName[5]>>8);
                putch('\n');
#endif
                SetHookFunction((u_int16)IdleHook, UartIdleHook);
#endif /*UART_CONTROL*/

                PlayerVolume(); /*required for QUIET_CANCEL*/
                PlayCurrentFile(); // Decode and Play.

#ifdef USE_USB
                if (USBIsAttached()) {
        	gotusb:
#if 0
                    /* SPIplayer: Start over by watchdog reset. */
                    USEX(WDOG_CONFIG) = 10;
                    USEX(WDOG_RESET) = 0x4ea9; /*Activate watchdog*/
#else
        	    ResetIRAMVectors();
        	    PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE1;
        	    SpiLoad(0+4, 1/*24-bit address*/);
#endif
                    while (1)
                        ;
                }
#endif/*USE_USB*/

#ifdef UART_CONTROL
                SetHookFunction((u_int16)IdleHook, NullHook);
#ifdef GAPLESS
        	putch('d');
#else
                putstrp("\pdone\n");
#endif
#endif /*UART_CONTROL*/
            } else {
                player.nextFile = 0;
            }
        }
    } else {
#ifdef UART_CONTROL
        putstrp("\pnofat\n");
#endif /*UART_CONTROL*/
    noFSnorFiles:
#ifdef GAPLESS
        codecVorbis.audioBegins = 0;
#endif
#ifdef USE_USB
        if (USBIsAttached()) {
            goto gotusb;
        }
#endif/*USE_USB*/
        LoadCheck(&cs, 32);
        memset(tmpBuf, 0, sizeof(tmpBuf)); /* silence */
        AudioOutputSamples(tmpBuf, sizeof(tmpBuf)/2); /* play silence */
    }
  }
}
