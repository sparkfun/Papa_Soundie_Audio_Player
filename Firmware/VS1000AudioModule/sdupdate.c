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

//#define USE_DEBUG
//#define DEBUG_LEVEL 3

#define USE_HC      /* Support SD-HC cards. */
//#define DELETE_SDUPDATE /* Delete SDUPDATE.PRG from SD card before performing firmware or content update. */

#include <dev1000.h>


#ifdef DELETE_SDUPDATE
auto s_int16 FatDeleteDirEntry(const u_int16 *packedName /*8.3 name*/);
//void FsFatFree(u_int32 start, u_int32 length);
#endif //DELETE_SDUPDATE

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
} mmc;


static char hex[] = "0123456789ABCDEF";
void puthex(u_int16 d) {
        register int i;
        for (i=0;i<4;i++) {
        	putch(hex[(d>>12)&15]);
        	d <<= 4;
        }
}
/*
  put packed string
 */
void putstrp(register __i0 u_int16 *packedStr) {
        while (1) {
        	register int i = *packedStr++;
        	if (i >> 8) {
        		putch(i >> 8);
        	} else {
        		break;
        	}
        	if (i & 255) {
        		putch(i);
        	} else {
        		break;
        	}
        }
}


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
    /*CMD8 is mandatory before ACMD41
      for hosts compliant to phys. spec. 2.00 */
    i = MmcCommand(MMC_SEND_IF_COND/*CMD8*/|0x40,
        	   0x00000122/*2.7-3.6V*/); /*note: 0x22 for the right CRC!*/
#if DEBUG_LEVEL > 2
    puthex(i);
    puts("=IF_COND");
#endif
    if (i == 1) {
        /* MMC answers: 0x05 illegal command,
           v2.0 SD(HC-SD) answers: 0x01 */
        /* Should we read the whole R7 response? */
#if DEBUG_LEVEL > 1
        //SpiSendReceiveMmc(-1, 32);
        puthex(SpiSendReceiveMmc(-1, 16));
        puthex(SpiSendReceiveMmc(-1, 16));
        puts("=R7");
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
#if 0
            /* Retry with CMD1 */
            if (c == 0xff && i == 2) {
        	cmd = MMC_SEND_OP_COND|0x40;
        	continue;
            }
#endif

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
        //SpiSendReceiveMmc(-1, 16);
    }
#endif /*USE_HC*/

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
            c_size <<= 2 + c_mult + (minifatBuffer[2] & 15);
            mmc.blocks = c_size >> 9;
        }
#if DEBUG_LEVEL > 1
        puts("=CSD");
        puthex(mmc.blocks>>16);
        puthex(mmc.blocks);
        puts("=mmcBlocks");
#endif
    }
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

#if 1
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
    map->blocks = mmc.blocks;
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
#if 0
            putch('R');
#endif
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

const struct FsMapper mmcMapper = {
    0x010c, /*version*/
    256,    /*blocksize in words*/
    0,      /*blocks -- read from CSD*/
    0,      /*cacheBlocks*/
    FsMapMmcCreate,
    FsMapFlNullOk,//RamMapperDelete,
    FsMapMmcRead,
#ifdef DELETE_SDUPDATE
        FsMapMmcWrite,
#else
    FsMapFlNullOk, //FsMapMmcWrite,
#endif
    NULL,//FsMapFlNullOk,//RamMapperFree,
    FsMapFlNullOk,//RamMapperFlush,
    NULL /* no physical */
};

struct FsMapper *FsMapMmcCreate(struct FsPhysical *physical,
        			u_int16 cacheSize) {
    PERIP(GPIO0_MODE) &= ~(MMC_MISO|MMC_CLK|MMC_MOSI|MMC_XCS);
    PERIP(GPIO0_DDR) = (PERIP(GPIO0_DDR) & ~(MMC_MISO))
        | (MMC_CLK|MMC_MOSI|MMC_XCS);
    PERIP(GPIO0_ODATA) = (PERIP(GPIO0_ODATA) & ~(MMC_CLK|MMC_MOSI))
        | (MMC_XCS | GPIO0_CS1); /* NFCE high */
#if DEBUG_LEVEL > 1
    puts("Configured MMC pins\n");
#endif
    memset(&mmc, 0, sizeof(mmc));
    return &mmcMapper;
}
u_int16 FsMapMmcRead(struct FsMapper *map, u_int32 firstBlock,
        	     u_int16 blocks, u_int16 *data) {
    register u_int16 bl = 0;
#ifndef PATCH_LBAB /*if not patched already*/
    firstBlock &= 0x00ffffff; /*remove sign extension: 4G -> 8BG limit*/
#endif
    while (bl < blocks) {
        if (MyReadDiskSector(data, firstBlock))
            break; /* probably MMC detached */
        data += 256;
        firstBlock++;
        bl++;
    }
    return bl;
}

#ifdef DELETE_SDUPDATE
u_int16 FsMapMmcWrite(struct FsMapper *map, u_int32 firstBlock, u_int16 blocks, u_int16 *data) {
        u_int16 bl = 0;
        firstBlock &= 0x00ffffff; /*remove sign extension: 4G -> 8BG limit*/
        while (bl < blocks) {
        	register u_int16 c;
        	if (mmc.state == mmcNA || mmc.errors) {
        		break;
        	}
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
        	firstBlock++;
        	bl++;
        }
        out:
        SpiSendClocks();
        return bl;
}
#endif/**/


/**************************************************************************/
#define SPI_EEPROM_COMMAND_WRITE_ENABLE  0x06
#define SPI_EEPROM_COMMAND_WRITE_DISABLE  0x04
#define SPI_EEPROM_COMMAND_READ_STATUS_REGISTER  0x05
#define SPI_EEPROM_COMMAND_WRITE_STATUS_REGISTER  0x01
#define SPI_EEPROM_COMMAND_READ  0x03
#define SPI_EEPROM_COMMAND_WRITE 0x02
#define SPI_EEPROM_COMMAND_CLEAR_ERROR_FLAGS 0x30
#define SPI_EEPROM_COMMAND_ERASE_SECTOR 0xD8
#define SPI_EEPROM_COMMAND_ERASE_4K_SECTOR 0x20


//MASTER, 8BIT, FSYNC PIN IDLE=1
#define SPI_MASTER_8BIT_CSHI   PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE1

//MASTER, 8BIT, FSYNC PIN IDLE=0 (makes /CS)
#define SPI_MASTER_8BIT_CSLO   PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE0

//MASTER, 16BIT, FSYNC PIN IDLE=0 (makes /CS)
#define SPI_MASTER_16BIT_CSLO  PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN16 | SPI_CF_FSIDLE0

void SingleCycleCommand(u_int16 cmd){
  SPI_MASTER_8BIT_CSHI; 
  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive(cmd);
  SPI_MASTER_8BIT_CSHI;
}


/// Wait for not_busy (status[0] = 0) and return status
u_int16 SpiWaitStatus(void) {
  u_int16 status;
  SPI_MASTER_8BIT_CSHI;
  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive(SPI_EEPROM_COMMAND_READ_STATUS_REGISTER);
  while ((status = SpiSendReceive(0)) & 0x01){
    // puthex(status);puts("=status");
  }
  ; //Wait until ready
  SPI_MASTER_8BIT_CSHI;
  return status;
}

#if 0
u_int16 SpiReadBlock(u_int16 blockn, u_int16 *dptr) {
  SpiWaitStatus();
  
  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive(SPI_EEPROM_COMMAND_READ);
  SpiSendReceive(blockn>>7);            // Address[23:16] = blockn[14:7]
  SpiSendReceive((blockn<<1)&0xff);     // Address[15:8]  = blockn[6:0]0
  SpiSendReceive(0x00);                    // Address[7:0]   = 00000000
  SPI_MASTER_16BIT_CSLO;
  {
    int n;
    for (n=0; n<256; n++){
      *dptr++ = SpiSendReceive(0);
    }
  }
  SPI_MASTER_8BIT_CSHI;

  return 0;
}
#endif

#if 0
u_int16 SpiVerifyBlock(u_int16 blockn, u_int16 *dptr) {
  SpiWaitStatus();
  
  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive(SPI_EEPROM_COMMAND_READ);
  SpiSendReceive(blockn>>7);            // Address[23:16] = blockn[14:7]
  SpiSendReceive((blockn<<1)&0xff);     // Address[15:8]  = blockn[6:0]0
  SpiSendReceive(0x00);                    // Address[7:0]   = 00000000
  SPI_MASTER_16BIT_CSLO;
  {
    int n;
    for (n=0; n<256; n++){
      if (*dptr++ != SpiSendReceive(0)) {
        SPI_MASTER_8BIT_CSHI;	
        return 1;
      }
    }
  }
  SPI_MASTER_8BIT_CSHI;
  return 0;
}
#endif

/// Write a block to EEPROM. Caution: Does not erase block
/// \param blockn number of sector to write: 0..32767 (16Mbytes)
/// \param dptr pointer to data block
u_int16 SpiWriteBlock(u_int16 blockn, u_int16 *dptr) {
  
  SingleCycleCommand(SPI_EEPROM_COMMAND_WRITE_ENABLE);
  SingleCycleCommand(SPI_EEPROM_COMMAND_CLEAR_ERROR_FLAGS);
  SingleCycleCommand(SPI_EEPROM_COMMAND_WRITE_ENABLE);

  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive(SPI_EEPROM_COMMAND_WRITE_STATUS_REGISTER);
  SpiSendReceive(0x02); //Sector Protections Off
  SPI_MASTER_8BIT_CSHI;

  SpiWaitStatus();
  SingleCycleCommand(SPI_EEPROM_COMMAND_WRITE_ENABLE);
  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive(SPI_EEPROM_COMMAND_WRITE);
  SpiSendReceive(blockn>>7);            // Address[23:16] = blockn[14:7]
  SpiSendReceive((blockn<<1)&0xff);     // Address[15:8]  = blockn[6:0]0
  SpiSendReceive(0);                    // Address[7:0]   = 00000000
 
  SPI_MASTER_16BIT_CSLO;
  {
    u_int16 n;
    for (n=0; n<128; n++){
      SpiSendReceive(*dptr++);
    }
  }
  SPI_MASTER_8BIT_CSHI;

  SpiWaitStatus();
  SingleCycleCommand(SPI_EEPROM_COMMAND_WRITE_ENABLE);
  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive(SPI_EEPROM_COMMAND_WRITE);
  SpiSendReceive(blockn>>7);       
  SpiSendReceive(((blockn<<1)+1)&0xff); // Address[15:8]  = blockn[6:0]1
  SpiSendReceive(0);              
  
  SPI_MASTER_16BIT_CSLO;
  {
    int n;
    for (n=128; n<256; n++){
      SpiSendReceive(*dptr++);
    }
  }
  SPI_MASTER_8BIT_CSHI;
  SpiWaitStatus();

  return 0;
}


#if 0
/// Erase one erasable block
/// Erase size for intel flash: 64K (128 disk sectors)
void SpiEraseBlock(u_int16 blockn_inside_sector){
  SingleCycleCommand(SPI_EEPROM_COMMAND_WRITE_ENABLE);  
  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive(SPI_EEPROM_COMMAND_WRITE_STATUS_REGISTER);
  SpiSendReceive(0x02); //Sector Protections Off
  SPI_MASTER_8BIT_CSHI;
  SpiWaitStatus();
  SingleCycleCommand(SPI_EEPROM_COMMAND_WRITE_ENABLE);  
  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive(SPI_EEPROM_COMMAND_ERASE_SECTOR);
  SpiSendReceive(blockn_inside_sector>>7);       
  SpiSendReceive((blockn_inside_sector<<1)&0xff);
  SpiSendReceive(0);              
  SPI_MASTER_8BIT_CSHI;
  SpiWaitStatus();
}
#endif

void EeUnprotect(){
  SingleCycleCommand(SPI_EEPROM_COMMAND_WRITE_ENABLE);
  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive(SPI_EEPROM_COMMAND_WRITE_STATUS_REGISTER);
  SpiSendReceive(0x02); //Sector Protections Off
  SPI_MASTER_8BIT_CSHI;
  SpiWaitStatus();
  SingleCycleCommand(SPI_EEPROM_COMMAND_WRITE_ENABLE);
}
void Erase4kBlock(u_int16 blockn) {
    // Erase 4K sector
    SingleCycleCommand(SPI_EEPROM_COMMAND_WRITE_ENABLE);
    SingleCycleCommand(SPI_EEPROM_COMMAND_CLEAR_ERROR_FLAGS);
    EeUnprotect();
    SPI_MASTER_8BIT_CSLO;
    SpiSendReceive(SPI_EEPROM_COMMAND_ERASE_4K_SECTOR);
    SpiSendReceive(blockn>>7);            // Address[23:16] = blockn[14:7]
    SpiSendReceive((blockn<<1)&0xff);     // Address[15:8]  = blockn[6:0]0
    SpiSendReceive(0);                    // Address[7:0]   = 00000000
    SPI_MASTER_8BIT_CSHI;
    SpiWaitStatus();
}




void main(void) {
        ResetIRAMVectors();

        InitAudio(); /* goto 3.0x..4.0x */
        PERIP(INT_ENABLEL) = INTF_RX | INTF_TIM0;
        PERIP(INT_ENABLEH) = INTF_DAC;
        Enable();

        SetHookFunction((u_int16)OpenFile, Fat12OpenFile);
        SetHookFunction((u_int16)IdleHook, NullHook);     /* no default keyscan */
        SetHookFunction((u_int16)USBSuspend, NullHook);   /* no low-power state */

        putstrp("\pSDUPDATE.PRG here, initializing MMC\n");

        map = FsMapMmcCreate(NULL, 0); /* Create MMC Mapper */

        while (1) {
        	if ((PERIP(GPIO0_DDR) & (1<<14)) == 0 && (PERIP(GPIO0_IDATA) & (1<<14))) {
        		/* SD removed */
        		USEX(WDOG_CONFIG) = 10;
        		USEX(WDOG_RESET) = 0x4ea9; /*Activate watchdog*/
        		while (1) {
        		}
        	}

        	/* If MMC is not available, try to reinitialize it. */
        	if (mmc.state == mmcNA || mmc.errors) {
        		/* enable SD power */
#define GPIO0_SD_POWER GPIO0_ALE
        		PERIP(GPIO0_DDR) |= GPIO0_SD_POWER;
        		PERIP(GPIO0_SET_MASK) = GPIO0_SD_POWER;

        		putstrp("\pInitializeMmc(50)\n");
        		InitializeMmc(50);
        	}

        	/* Try to init FAT. */
        	if (InitFileSystem() == 0) {
        		/*
        		Check if there is an update file (SDUPDATE.PRG) on the SD card.
        		*/
        		int programmed = 0;
        
        		putstrp("\pInitFileSystem() ok\n");

#ifdef DELETE_SDUPDATE
        		/* Locate SDUPDATE.PRG, delete it and free the allocated cluster list. */
        		if (OpenFileNamed("\pSDUPDATE", FAT_MKID('P','R','G')) != 0xffffU) {
        			__y struct FRAGMENT *frag = &minifatFragments[0];
        			putstrp("\pSDUPDATE.PRG found, deleting\n");
        			FatDeleteDirEntry("\pSDUPDATEPRG");
        		}
#endif
        		/*DEBUG -- turn on Red LED. */
        		PERIP(GPIO0_MODE) &= ~((1<<5) | (1<<10));
        		PERIP(GPIO0_DDR) |= (1<<5) | (1<<10);
        		PERIP(GPIO0_SET_MASK) = (1<<10);

        		if (OpenFileNamed("\pCONTENT ",FAT_MKID('R','A','W')) != 0xffffU) {
        			u_int16 sector = RESERVED_BLOCKS; //Content only
        
        			putstrp("\pCONTENT.RAW found\n");

        			while (ReadFile(mallocAreaX, 0, 4096/*4kbyte*/)) {
        				int s;
        				Erase4kBlock(sector);
        				programmed = 1;
        
        				/*DEBUG -- toggle Green LED */
        				PERIP(GPIO0_ODATA) ^= (1<<5);
        				for (s=0;s<8;s++) {
#if USE_INVERTED_DISK_DATA
        					/*if (sector >= RESERVED_BLOCKS)*/
        					{
        						register int j;
        						register u_int16 *p = mallocAreaX+256*s;
        						for (j=0;j<256;j++) {
        							*p = ~*p;
        							p++;
        						}
        					}
#endif
        					SpiWriteBlock(sector, mallocAreaX+256*s);
        					sector += 1;
        				}
        				/* Check that we do not write too much. */
        				if (sector >= CHIP_TOTAL_BLOCKS) {
        					break;
        				}
        			}
        		}
        		/* Then check FIRMWARE.RAW */
        		if (OpenFileNamed("\pFIRMWARE",FAT_MKID('R','A','W')) != 0xffffU) {
        			u_int16 sector = 0;//Firmware
        
        			putstrp("\pFIRMWARE.RAW found\n");

        			while (ReadFile(mallocAreaX, 0, 4096/*4kbyte*/)) {
        				int s;
        				Erase4kBlock(sector);
        				programmed = 1;

        				/*DEBUG -- toggle Green LED */
        				PERIP(GPIO0_ODATA) ^= (1<<5);
        				for (s=0;s<8;s++) {
        					SpiWriteBlock(sector, mallocAreaX+256*s);
        					sector += 1;
        				}
        				/* Check that we do not write too much. */
        				if (sector >= RESERVED_BLOCKS) {
        					break;
        				}
        			}
        		}

        		if (programmed) {
        			/*DEBUG -- turn on Green LED, turn off Red LED. */
        			PERIP(GPIO0_SET_MASK) = (1<<5);
        			PERIP(GPIO0_CLEAR_MASK) = (1<<10);
        
        			putstrp("\pFinished, remove SD card\n\n");
#if 1
        			/*beep*/
        			SetRate(32000U);
        			{
        				register int i;
        				for (i=0;i<4096;i+=32) {
        					memset(tmpBuf, -0x0fff, 2*16);
        					AudioOutputSamples(tmpBuf, 16);
        					memset(tmpBuf, 0x0fff, 2*16);
        					AudioOutputSamples(tmpBuf, 16);
        				}
        			}
#endif

        			voltages[voltIoPlayer]    = 0; /*dim yellow LED*/
        			PowerSetVoltages(&voltages[voltCorePlayer]);
        			while (1) {
        				if ((PERIP(GPIO0_DDR) & (1<<14)) == 0 && (PERIP(GPIO0_IDATA) & (1<<14))) {
        					/* SD removed */
        					USEX(WDOG_CONFIG) = 10;
        					USEX(WDOG_RESET) = 0x4ea9; /*Activate watchdog*/
        					while (1) {
        					}
        				}
        			}
        		} else {
        			/* If neither CONTENT.RAW nor FIRMWARE.RAW found. */
        			int i;
        			while (1) {
        				BusyWait10();
        				i++;
        				if ((i & 15) == 0) {
        					PERIP(GPIO0_SET_MASK) = (1<<5);
        				} else {
        					PERIP(GPIO0_CLEAR_MASK) = (1<<5);
        				}
        				if ((PERIP(GPIO0_DDR) & (1<<14)) == 0 && (PERIP(GPIO0_IDATA) & (1<<14))) {
        					/*removed = RED LED on */
        					PERIP(GPIO0_SET_MASK) = (1<<10);
        				} else {
        					/*SD switch on = off */
        					PERIP(GPIO0_CLEAR_MASK) = (1<<10);
        				}
        			}
        		}
        	} else {
        		/* If not a valid FAT (perhaps because MMC/SD is not inserted),
        		just send some samples to audio buffer and try again. */
        		noFSnorFiles:
        		LoadCheck(&cs, 32); /* decrease or increase clock */
        		memset(tmpBuf, 0, sizeof(tmpBuf));
        		AudioOutputSamples(tmpBuf, sizeof(tmpBuf)/2);
        		/* When no samples fit, calls the user interface
        		-- handles volume control and power-off. */
        	}
        }
}
