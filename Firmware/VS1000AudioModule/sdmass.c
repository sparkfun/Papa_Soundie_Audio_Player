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

#define GPIO0_SD_POWER GPIO0_ALE

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

#define USE_USB /* You get more free code space if you disable USB. */
#ifdef DISABLE_USB
#undef USE_USB
#endif

#define USE_MULTIPLE_BLOCK_WRITE /*doubles write speeds on SD cards*/
//#define USE_DEBUG
//#define DEBUG_LEVEL 3

#define USE_HC      /* Support SD-HC cards. */
#define PATCH_EJECT    /* 'eject' shuts down power -- not 100% working now */
#define PATCH_UNIT_READY /* detects card removed during USB mode */
#define PATCH_SENSE      /* patches REQUEST_SENSE */


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

#ifdef USE_USB
    if (MmcCommand(MMC_SEND_CSD/*CMD9*/|0x40, 0) == 0) {
        register s_int16 *p = (s_int16 *)minifatBuffer;
        register int t = 640;
        while (SpiSendReceiveMmc(0xff00, 8) == 0xff) {
            if (t-- == 0) {
#if DEBUG_LEVEL > 1
                puts("Timeout 3");
#endif
                goto tryagain;
            }
        }
        for (i=0; i<8; i++) {
            *p++ = SpiSendReceiveMmc(-1, 16);
#if DEBUG_LEVEL > 1
            puthex(p[-1]);
#endif
/*
            00 00 11 11 222 2 3 3334444555566667777
  64M  MMC: 8C 0E 01 2A 03F 9 8 3D3F6D981E10A4040DF
            CSD  NSAC   CCC  flg       MUL+E
              TAAC  SPEE   RBL  C_SIZE(>>2=FDB)
                                33 333333333344 444 444 444 444 445
                                00 111101001111 110 110 110 110 011 000000111
                                   C_SIZE                       MULT
  256M MMC+ 90 5E 00 2A 1F5 9 8 3D3EDB683FF9640001F=CSD (Kingston)
  128M SD:  00 26 00 32 175 9 8 1E9F6DACF809640008B=CSD (DANE-ELEC)

  4G SD-HC: 40 7F 0F 32 5B5 9 8 0001E44 7F801640004B
            00 00 11 11 222 2 3 3334444 555566667777


 */
        }
        if ((minifatBuffer[0] & 0xf000) == 0x4000) {
            /* v2.0 in 512kB resolution */
          //puts("v2");
            mmc.blocks = 
                (((((u_int32)minifatBuffer[3]<<16) | minifatBuffer[4])
                 & 0x3fffff) + 1) << 10;
        } else {
            /* v1.0 */
            register s_int32 c_size = (((minifatBuffer[3] & 0x03ff)<<2) |
                                       ((minifatBuffer[4]>>14) & 3)) + 1;
            register s_int16 c_mult = (((minifatBuffer[4] & 3)<<1) |
                                       ((minifatBuffer[5]>>15) & 1));
            //puts("v1");
            c_size <<= (2-9) + c_mult + (minifatBuffer[2] & 15);
            mmc.blocks = c_size;
        }
#if DEBUG_LEVEL > 1
        puts("=CSD");
        puthex(mmc.blocks>>16);
        puthex(mmc.blocks);
        puts("=mmcBlocks");
#endif
    }
#endif/*USE_USB*/


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
#ifdef USE_USB
    map->blocks = mmc.blocks;
#endif
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

#ifdef USE_USB
const struct FsMapper mmcMapper = {
    0x010c, /*version*/
    256,    /*blocksize in words*/
    0,      /*blocks -- read from CSD*/
    0,      /*cacheBlocks*/
    NULL/*FsMapMmcCreate*/,
    FsMapFlNullOk,//RamMapperDelete,
    FsMapMmcRead,
    FsMapMmcWrite,
    NULL,//FsMapFlNullOk,//RamMapperFree,
#ifdef USE_MULTIPLE_BLOCK_WRITE
    FsMapMmcFlush,
#else
    FsMapFlNullOk,//RamMapperFlush,
#endif
    NULL /* no physical */
};
#endif/*USE_USB*/

#ifdef USE_USB
struct FsMapper *FsMapMmcCreate(struct FsPhysical *physical,
                                u_int16 cacheSize) {
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
    return &mmcMapper;
}
#endif

#ifdef USE_USB
u_int16 FsMapMmcRead(struct FsMapper *map, u_int32 firstBlock,
                     u_int16 blocks, u_int16 *data) {
#ifdef USE_MULTIPLE_BLOCK_WRITE
    /* with "if" it is a little faster - but more code */
    //if (mmc.nextWriteBlock != 0xffffffff)
    {
        /* Finalize write mode */
        FsMapMmcWrite(NULL, 0, 0, NULL);
    }
#endif
    /* We know this is called one block at a time by the USB ROM firmware,
       so only prepare for that and save 20 words. */
    /*remove sign extension: 4G -> 8BG limit*/
    if (MyReadDiskSector(data, firstBlock & 0x00ffffff))
        return 0; /* probably MMC detached */
    return 1;
}
u_int16 FsMapMmcWrite(struct FsMapper *map, u_int32 firstBlock,
                      u_int16 blocks, u_int16 *data) {
    /* We know this is called one block at a time by the USB ROM firmware,
       so only prepare for that and save 14 words. */
    firstBlock &= 0x00ffffff; /*remove sign extension: 4G -> 8BG limit*/
#ifdef USE_MULTIPLE_BLOCK_WRITE
    if (!data || firstBlock != mmc.nextWriteBlock) {
        /* Finalize write mode */
        if (mmc.nextWriteBlock != 0xffffffff) {
            mmc.nextWriteBlock = 0xffffffff;
            SpiSendReceiveMmc(0xfd00, 8); /* End of write */
            do {
                /*TODO: timeout?*/
            } while ( SpiSendReceiveMmc(0xff00U, 8) != 0x00ffU);
            SpiSendClocks();
        }
        if (!data) {
            return 0;
        }
    }
#endif

    {
        register u_int16 c;
        if (mmc.state == mmcNA || mmc.errors)
            return 0;
#ifdef USE_MULTIPLE_BLOCK_WRITE
        if (mmc.nextWriteBlock == 0xffffffff) {
            mmc.nextWriteBlock = firstBlock;
#ifdef USE_HC
            c = MmcCommand(MMC_WRITE_MULTIPLE_BLOCK|0x40, firstBlock<<mmc.hcShift);
#else
            c = MmcCommand(MMC_WRITE_MULTIPLE_BLOCK|0x40, firstBlock<<9);
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
        }
        mmc.nextWriteBlock++;
        SpiSendReceiveMmc(0xfc00, 8);
#else/*MULTIPLE*/

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
#endif/*else MULTIPLE*/

        for (c=0; c<256; c++) {
            SpiSendReceiveMmc(*data++, 16);
        }
        SpiSendReceiveMmc(0xffff, 16); /* send dummy CRC */

        if (map) {
            /* wait until MMC write finished */
            do {
                /*TODO: timeout?*/
            } while ( SpiSendReceiveMmc(0xff00U, 8) != 0x00ffU);
        } else {
            mmc.state = mmcWriteWait;
            /* MUST not use SpiSendClocks():
               some MMC/SD's don't support interrupted XCS. */
            return 0; /*Can write only one sector this way.*/
        }
    }
 out:
#ifdef USE_MULTIPLE_BLOCK_WRITE
    if (mmc.nextWriteBlock != 0xffffffff) {
        /* Keep the write open */
    } else {
        SpiSendClocks();
    }
#else
    SpiSendClocks();
#endif
    return 1;
}
#endif/*USE_USB*/

#ifdef USE_USB
#if defined(PATCH_EJECT) || defined(PATCH_UNIT_READY) || defined(PATCH_SENSE)
#include <scsi.h>
#endif
#ifdef PATCH_EJECT
int startUnit = 1;
#endif

/*
  We replace the MassStorage() functionality.
  Uses ScsiTestUnitReady() to check the MMC/SD removal.
  Only appears as a mass storage device when MMC/SD is present.
 */
auto void MyMassStorage(void) {
  if (mmc.state == mmcNA || mmc.errors) {
#ifdef USE_DEBUG
      puts("No MMC -> no USB MassStorage");
#endif
      return;
  }

  PowerSetVoltages(&voltages[voltCoreUSB]);
  /** a 2.5ms-10ms delay to let the voltages change (rise) */
//  BusyWait10();

  LoadCheck(NULL, 1); /* 4.0x clock required! */
  InitUSB(USB_MASS_STORAGE);  
  USB.lastSofTime = ReadTimeCount();

  while (mmc.state != mmcNA && mmc.errors == 0) {
    USBHandler();

    /* If no SOF in such a long time.. */
    if (USBWantsSuspend()
#if 1 /* Allow detach detection even when we have not yet been configured */
        || (USB.lastSofTime && ReadTimeCount() - USB.lastSofTime > 2*TIMER_TICKS)
#endif
        ) {
      /* Do we seem to be detached?
         Note: Now also requires USBWantsSuspend() to return TRUE! */
      if (USBIsDetached()) {
        break;
      }
#ifdef USE_DEBUG
      putstrp("\pSuspend\n");
#endif
      /*Suspend is not supported anyway*/
      USB.lastSofTime = ReadTimeCount();
    }
#ifdef PATCH_EJECT
    if (startUnit == 2) {
        break;
    }
#endif
  }
//  hwSampleRate = 1; /* Forces FREQCTL update at next SetRate() */
  PERIP(SCI_STATUS) &= ~SCISTF_USB_PULLUP_ENA; /* Take us 'out of the bus' */
  PERIP(USB_CONFIG) = 0x8000U; /* Reset USB */
#ifdef USE_MULTIPLE_BLOCK_WRITE
  map->Flush(map, 1);
#endif
  LoadCheck(NULL, 0);

  /* Set player voltages. No need to wait for the voltages to drop. */
  PowerSetVoltages(&voltages[voltCorePlayer]);
#ifdef PATCH_EJECT
  /* If 'eject', power down */
  if (startUnit == 2) {
      int i;
      /* We should drain the SD card first! */
      MmcCommand(MMC_GO_IDLE_STATE/*CMD0*/|0x40,0);
      PERIP(GPIO0_CLEAR_MASK) =
          (GPIO0_SD_POWER|MMC_MISO|MMC_CLK|MMC_MOSI|MMC_XCS);
#ifdef UART_CONTROL
      putstrp("\pEjected - Power Off\n\n");
#endif/*UART_CONTROL*/
      clockX = 2;
      SetRate(100);
      voltages[voltIoSuspend] = 0;
      PowerSetVoltages(&voltages[voltCoreSuspend]);

      /* Turn off LED's */
      PERIP(GPIO1_DDR)   |=  1;
      PERIP(GPIO1_MODE)  &= ~1;
      USEX(GPIO1_CLEAR_MASK) = 0x0f;
      USEX(GPIO0_CLEAR_MASK) = 0xffff;
      /*
        You may also need to draw IOVDD empty through RX pull-up
      */

      for (i=0;i<1000;i++) {
          BusyWait10();
      }
      RealPowerOff();
  }
#endif/*PATCH_EJECT*/
}
#endif /*USE_USB*/




#ifdef USE_USB
#if defined(PATCH_EJECT) || defined(PATCH_UNIT_READY) || defined(PATCH_SENSE)
#include <scsi.h>
auto u_int16 NewDiskProtocolCommand(register __i2 u_int16 *cmd) {
    __y u_int16 c = (cmd[OPERATION_CODE] & 0xff);
#if defined(PATCH_EJECT)
    if (c == SCSI_START_STOP_UNIT) {
        /* 0:OP 1:imm0 2:res 3:res 4:power7-4 res3-2 loej1 start0 5:control */
        startUnit = cmd[4] & 1;
    } else if (startUnit == 0) {
        /* The next command after STOP UNIT will not be serviced
           fully because we will turn ourselves off. */
        startUnit = 2;
    }
#endif
#if defined(PATCH_UNIT_READY)
    if (c == SCSI_TEST_UNIT_READY) {
        /* Poll MMC present by giving it a command.
           But only when idle, i.e. mmcOk. */
#ifdef USE_MULTIPLE_BLOCK_WRITE
        /* with "if" it is a little faster - but more code */
        //if (mmc.nextWriteBlock != 0xffffffff)
        {
            /* Finalize write mode */
            FsMapMmcWrite(NULL, 0, 0, NULL);
        }
#endif
        if (mmc.state == mmcOk && mmc.errors == 0 &&
            MmcCommand(MMC_SET_BLOCKLEN|0x40, 512) != 0) {
            mmc.errors++;
        }
        if (mmc.state == mmcNA || mmc.errors) {
            SCSI.Status = SCSI_REQUEST_ERROR; /* report error at least once! */
        }
        SCSI.State = SCSI_SEND_STATUS;
        return -1; /*done processing*/
    }
#endif
#if defined(PATCH_SENSE)
    if (c == SCSI_REQUEST_SENSE) {
        /* patch a bug -- sense was written to the wrong buffer */
        SCSI.DataBuffer[0] = 0x7000;
        SCSI.DataOutBuffer = SCSI.DataBuffer;
        SCSI.DataOutSize = 18; /*Windows requires 18.*/
        SCSI.State = SCSI_DATA_TO_HOST;
        return -1; /* done with the command */
    }
#endif
    return cmd[OPERATION_CODE]; /* perform the command */
}
#endif
#endif /*USE_USB*/



void CheckSd(void) {
    if ((PERIP(GPIO0_IDATA) & (1<<14))) {
        /* SD not inserted */
        putstrp("\p!SD\n");

        /* If SD is not inserted, run SPI FLASH player. */
#ifndef USE_USB
        //SetHookFunction((u_int16)ReadDiskSector, MapperReadDiskSector);
#endif
        PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE1;
        ResetIRAMVectors();
        SpiLoad(SPIPLAYERSTART+4, 1/*24-bit address*/);
    }
}

void IterateFilesCallback(register __b0 u_int16 *name) {
    register int i;
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


void main(void) {
    Enable();

#if 0
    InitAudio(); /* goto 3.0x..4.0x */
    PERIP(INT_ENABLEL) = INTF_RX | INTF_TIM0;
    PERIP(INT_ENABLEH) = INTF_DAC;
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

#ifdef USE_USB
#if 0
    /*Contains LBAB patch,but we have a 4GB workround in read/write*/
    SetHookFunction((u_int16)MSCPacketFromPC, PatchDiskProtocolCommandC);
#else
    /*fixes SCSI_READ_10 residue problem -- mainly for Linux compatibility,
      does not fix LBAB, but is shorter.. */
    SetHookFunction((u_int16)MSCPacketFromPC, PatchDiskProtocolCommandDE);
#endif
#endif/*USE_USB*/

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
#ifdef USE_USB
    map = FsMapMmcCreate(NULL, 0); /* Create MMC Mapper */
#endif

//    player.volume = -24; /* "max" volume */
    //PlayerVolume();

#ifdef UART_CONTROL
    UartInit(); //Set interrupt etc.
#endif /*UART_CONTROL*/
#ifdef GPIO_CONTROL
    GPIOInit();
#endif /*GPIO_CONTROL*/

    CheckSd();

#ifdef USE_USB
    /* If USB is attached, call our version of the MassStorage()
       handling function. */
    if (USBIsAttached()) {
#ifdef UART_CONTROL
        putstrp("\pUSB Attach SD\n");
#endif/*UART_CONTROL*/
        while (USBIsAttached()) {
            /* If MMC is not available, try to reinitialize it. */
            if (mmc.state == mmcNA || mmc.errors) {
        	/* enable SD power */
        	PERIP(GPIO0_SET_MASK) = GPIO0_SD_POWER;
        	InitializeMmc(50);

        	CheckSd(); // If we don't have SD, jump to SPIPLAYER
            } else {
        	MyMassStorage();
            }
        }
#ifdef UART_CONTROL
        putstrp("\pUSB Detach SD\n");
#endif/*UART_CONTROL*/
    }
#endif /*USE_USB*/

    CheckSd(); // If we don't have SD, jump to SPIPLAYER

    PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE1;
    ResetIRAMVectors();
    SpiLoad(SDPLAYERSTART+4, 1/*24-bit address*/);

}
