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
#include <vsNand.h>

#include <codec.h>
#include <player.h>
#include "system.h"

//#define RESERVED_BLOCKS 32

__y const char hex[] = "0123456789abcdef";
void puthex(u_int16 a) {
  char tmp[8];
  tmp[0] = hex[(a>>12)&15];
  tmp[1] = hex[(a>>8)&15];
  tmp[2] = hex[(a>>4)&15];
  tmp[3] = hex[(a>>0)&15];
  tmp[4] = ' ';
  tmp[5] = '\0';
  fputs(tmp, stdout);
}

void put2c(u_int16 a) {
  char tmp[8];
  tmp[0] = a>>8;
  tmp[1] = a&0xff;
  if (tmp[0]<32) tmp[0]='.';
  if (tmp[1]<32) tmp[1]='.';
  if (tmp[0]>127) tmp[0]='.';
  if (tmp[1]>127) tmp[1]='.';
  tmp[2] = '\0';
  fputs(tmp, stdout);
}

void put2hex(u_int16 a) {
  char tmp[8];
  tmp[0] = hex[(a>>12)&15];
  tmp[1] = hex[(a>>8)&15];
  tmp[2] = ' ';
  tmp[3] = hex[(a>>4)&15];
  tmp[4] = hex[(a>>0)&15];
  tmp[5] = ' ';
  tmp[6] = '\0';
  fputs(tmp, stdout);
}



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


void DumpBlock (u_int16 *dptr) {
  int n;
  for (n=0; n<256; n++){
    puthex(*dptr++);
    //put2c(*dptr++);
  }
  puts("");
}



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

void EraseAll(){
  register u_int16 i;
  u_int16 sn;
  puts("Blank Checking and erasing as needed up to 128KiB");
  for (sn = 0; sn<511; sn++){ //from 0 up to 16M
    SpiReadBlock(sn, minifatBuffer);
    i=0;   
    while (i<256) {
      if (minifatBuffer[i]!=0xffff){
	puthex(sn);
	SpiEraseBlock(sn);
	puts("erased");
	i=256;
      }
      i++;
    }
  }
}    



void ReadAll(){
  register u_int16 i;
  u_int16 sec,off;
  for (sec=0; sec<32768; sec++){ //from 0 up to 16M
    SpiReadBlock(sec,minifatBuffer);
    for (off=0; off<256; off+=8){
      puthex(sec*512+off*2);
      for (i=0; i<8; i++){
	put2hex (minifatBuffer[off+i]);
      }
      for (i=0; i<8; i++){
	put2c (minifatBuffer[off+i]);
      }
      fputs("\n",stdout);
      if (ReadGPIO()&0x01) return;
    }   
  }
}


#define INPUT_NAME "spiall.spi"
//#define INPUT_NAME "eeprom.img"

void Prom_vs1000img(){

  FILE *fp;
  puts("Programming " INPUT_NAME ". This does not erase old image.");

  //Detach vs3emu debugger interface interrupt for binary xfer...
  PERIP(INT_ENABLEL) &= ~INTF_RX;

  if (fp = fopen (INPUT_NAME, "rb")){
    u_int16 len;
    u_int16 sectorNumber;
    
    sectorNumber = 0;

    while ((len=fread(minifatBuffer,1,256,fp))){
      SpiWriteBlock(sectorNumber, minifatBuffer);

      if (SpiVerifyBlock(sectorNumber, minifatBuffer)){
	puts("Verify Error. You may need to erase chip.");
	PERIP(INT_ENABLEL) |= INTF_RX;
	return;
      }

      sectorNumber++;
      puthex(sectorNumber);
      //puts("=sect");
    }
    fclose(fp);       
  }else{
    puts("File not found\n");
  }

  //Reattach vs3emu debugger interface interrupt
  PERIP(INT_ENABLEL) |= INTF_RX;
  puts("Done.");
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
  char s[5];
  u_int16 i,j;

  // init spi
  SPI_MASTER_8BIT_CSHI;
  PERIP(SPI0_FSYNC) = 0; 
  PERIP(SPI0_CLKCONFIG) = SPI_CC_CLKDIV * (2-1); //clock divider 2

  PERIP(GPIO1_MODE) |= 0x1f; /* enable SPI pins */
  
  /* Make sure there is enough voltage to program the SPI memory. */
  if (voltages[voltIoPlayer] < 18) {
      voltages[voltIoPlayer]    = 18; /*2.7*/
      PowerSetVoltages(&voltages[voltCorePlayer]);
  }

  puts("\nVS1000 SPI EEPROM Write Utility");
  puts("Supports EEPROMS with 24bit address (128 kilobytes to 16 megabytes)\n");

#if 0
  SpiReadBlock(0, minifatBuffer);
  DumpBlock(minifatBuffer);
#endif

#if 1
  {
      int i = 0;
      puts("Erasing");
      while (i<RESERVED_BLOCKS) {
	  /* Only erase the firmware space */
	  puthex(i);
	  puts("");
	  Erase4kBlock(i);
	  i += 4;
      }
  }
#else
  EraseAll();
#endif
  Prom_vs1000img();
  SpiLoad(4,1);

}


