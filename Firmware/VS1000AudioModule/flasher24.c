// PROMMER for 24-bit addressable flashes like the INTEL S33,
// EON and WINBOND 128kilobyte...16megabyte SPI flashes

#include <stdio.h>

#include <string.h>
#include <stdlib.h>
#include <vs1000.h>
#include <audio.h>
#include <mappertiny.h>
#include <minifat.h>
#include <physical.h>
#include <usblowlib.h>
#include <vsNand.h>

#include <codec.h>
#include "player.h"

#define RESERVED_BLOCKS 32

extern struct FsPhysical *ph;
extern struct FsMapper *map;
extern __y u_int16 mallocAreaY[]; /* for ramdisk */
extern u_int16 mallocAreaX[];     /* for ramboot */
void SpiLoad(register __i2 short startAddr, register __i0 short m24);

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

void SingleCycleCommand(register __c0 u_int16 cmd){
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


/// Write a block to EEPROM. Caution: Does not erase block
/// \param blockn number of sector to write: 0..32767 (16Mbytes)
/// \param dptr pointer to data block
u_int16 SpiWriteBlock(u_int16 blockn, register u_int16 *dptr) {
  
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
  PERIP(SCI_STATUS) &= ~SCISTF_USB_PULLUP_ENA;
  PERIP(USB_CONFIG) = 0x8000U;

  // init spi
  SPI_MASTER_8BIT_CSHI;
  PERIP(SPI0_FSYNC) = 0; 
  PERIP(SPI0_CLKCONFIG) = SPI_CC_CLKDIV * (2-1); //clock divider 2

  PERIP(GPIO1_MODE) |= 0x1f; /* enable SPI pins */
  
  /* Only erase the firmware space */
  Erase4kBlock(0);
  Erase4kBlock(8);
  Erase4kBlock(16);
  Erase4kBlock(24);

  {
      register u_int16 gotFile = 0;
      if (InitFileSystem() == 0) {
	  static const u_int32 bootFiles[] = {
	      FAT_MKID('S','P','I'), 0
	  };
	  minifatInfo.supportedSuffixes = bootFiles;
	  if (OpenFile(0) < 0) {
	      gotFile = 1;
	  }
      }

      if (gotFile == 0) {
	  /* No file found, error! */
      } else {
	  register u_int16 sectorNumber = 0;
	  do {
	      register u_int16 len = 0;
	      memset(mallocAreaX, -1, 0x1000);
	      gotFile = ReadFile(mallocAreaX, 0, 2*0x1000) / 2;
	      while (len < gotFile) {
		  SpiWriteBlock(sectorNumber, mallocAreaX+len);
		  len += 256; /* words */
		  sectorNumber++;
		  //puthex(sectorNumber);
	      //puts("=sect");
	      }
	  } while (gotFile);
      }
      /* Programming done. */
  }
  SpiLoad(4,1); /* restart */
}


