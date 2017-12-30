/// \file dump.c Dump contents of SPI FLASH to disk
/*
  
  Copyright 2008-2010 VLSI Solution Oy. Absolutely no warranty.

*/  

#include "system.h"

#define SPI_CLOCK_DIVIDER 2

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

extern struct FsNandPhys fsNandPhys;
extern struct FsPhysical *ph;
extern struct FsMapper *map;
extern struct Codec *cod;
extern struct CodecServices cs;
extern u_int16 codecVorbis[];

//macro to set SPI to MASTER; 8BIT; FSYNC Idle => xCS high
#define SPI_MASTER_8BIT_CSHI   PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE1

//macro to set SPI to MASTER; 8BIT; FSYNC not Idle => xCS low
#define SPI_MASTER_8BIT_CSLO   PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE0

//macro to set SPI to MASTER; 16BIT; FSYNC not Idle => xCS low
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
  do {
    status = SpiSendReceive(0);
    if (PERIP(USB_STATUS) & USB_STF_BUS_RESET){
      USBHandler();
      SPI_MASTER_8BIT_CSHI;
      return -1; /* USB HAS BEEN RESET */
    }
  } while (status & 0x01);
    ; //Wait until chip is ready or return -1 if USB bus reset

  SPI_MASTER_8BIT_CSHI;
  
  return status;
}


void EePutReadBlockAddress(register u_int16 blockn){
  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive(SPI_EEPROM_COMMAND_READ);
  SpiSendReceive(blockn>>7);            // Address[23:16] = blockn[14:7]
  SpiSendReceive((blockn<<1)&0xff);     // Address[15:8]  = blockn[6:0]0
  SpiSendReceive(0);                    // Address[7:0]   = 00000000
  SPI_MASTER_16BIT_CSLO;
}

// Block Read for SPI EEPROMS with 24-bit address e.g. up to 16MB 
u_int16 EeReadBlock(u_int16 blockn, u_int16 *dptr) {
  SpiWaitStatus();
  
  EePutReadBlockAddress(blockn);
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


void InitSpi(u_int16 clockDivider){
  SPI_MASTER_8BIT_CSHI;
  PERIP(SPI0_FSYNC) = 0;
  PERIP(SPI0_CLKCONFIG) = SPI_CC_CLKDIV * (clockDivider-1);
  PERIP(GPIO1_MODE) |= 0x1f; /* enable SPI pins */
}



void main(void) {
  u_int32 sector = RESERVED_BLOCKS, empties = 0;
  FILE *out = NULL;

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

  InitSpi(SPI_CLOCK_DIVIDER);  

  puthex(CHIP_TOTAL_BLOCKS);
  puts("=total blocks");

  memset(mallocAreaX+512/2, 0, 512/2); /* empty */
  while (sector < CHIP_TOTAL_BLOCKS) {
      EeReadBlock(sector, mallocAreaX);
      if (memcmp(mallocAreaX, mallocAreaX+512/2, 512/2) == 0) {
	  empties++;
      } else {
	  if (!out) {
	      out = fopen("dump.raw", "wb");
	  }
	  if (out) {
	      while (empties) {
		  --empties;
		  fwrite(mallocAreaX+512/2, 1, 512/2, out);
	      }
	      fwrite(mallocAreaX, 1, 512/2, out);
	  }
      }
      if ((sector & 15) == 0) {
	  putchar('\r');
	  puthex(sector);
	  fflush(stdout);
      }
      sector++;
  }
  if (out) {
      fclose(out);
  }
  putchar('\n');
  puthex(empties);
  puts("=empty sectors at end, not dumped");
  puts("Dump finished, powering off");


  PowerOff();
  while (1)
      ;
}
