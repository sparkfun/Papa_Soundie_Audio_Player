/// \file usbmass.c Low-power SPI FLASH mass storage, see also spiplayer.c
// derived from: spiusb.c Winbond 25X16 SPI flash/USB/Vorbis player
/*
  
  Copyright 2008-2011 VLSI Solution Oy. Absolutely no warranty.

  SPI FLASH USB DRIVE  +  MUSIC PLAYER  +  USB AUDIO EXAMPLE
  ----------------------------------------------------------

  This example code uses the 18 kilobytes of mallocAreaY to implement
  a disk read/write cache and work are that is capable of erasing and 
  programming an SPI flash chip such as the Winbond 25X16 in 4 kilobyte 
  chunks when the VS1000B is connected to USB. Tested with Windows XP.
   
  Note: The SPI flash is quite slow to write to (about the speed of
  a floppy disk). Writing small files (a few kilobytes) is fast due
  to the cache system. Large files may cause timeouts. 
  If write timeouts happen, just try again and be patient.

  Note FAT12 Subdirectory Limitations.

  Requirements: An SPI flash eeprom with 4 kilobyte erasable blocks.

  Tested with VS1000B, Winbond 25X16 and Windows XP Professional.
  Measured performance: 1 megabyte file was written in 55 seconds when
  the eeprom was blank (no need to erase). When writing over already
  written data, writing the same file took 1 minute 35 seconds.

  At first connect the USB disk is unformatted.
  Suggestion: use "Quick Format" when formatting the disk with WinXP.


  20111209:
  Win2000 is confused about a composite device with both mass storage
  and audio device. It just stops talking to the mass storage after
  the first Request Sense.
  Win7 is only slightly better. It seems to lock up if audio and
  mass storage are used at the same time.

  So, the composite device was removed and the default VS1000
  mass storage descriptor is used.

*/  
  
#define POFFTIME 60 /*5 minutes*/
#define PATCH_SCSI_COMMAND /* now includes LBAB-patch */
//#define AUDIO_PLUS_MASS_STORAGE /* */
void JAfterSpi (void);
 
#include "system.h"
  
#define PATCH_EJECT    /* 'eject' shuts down power */
#define PATCH_SENSE      /* patches REQUEST_SENSE */
  
#ifdef UART_CONTROL
void putstrp (register __i0 u_int16 * packedStr);

#endif  /*  */
  
#define SPI_CLOCK_DIVIDER 4 // 2
  
// number of 512-blocks in RAM cache (MUST BE 16 DO NOT ALTER! I MEAN IT!)
#define CACHE_BLOCKS 16
   
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
   
#include <stdio.h>  // Standard io
#include <stdlib.h> // VS_DSP Standard Library
#include <vs1000.h> // VS1000B register definitions
#include <vectors.h>  // VS1000B vectors (interrupts and services)
#include <minifat.h>  // Read Only Fat Filesystem
#include <mapper.h> // Logical Disk
#include <string.h> // memcpy etc
#include <player.h> // VS1000B default ROM player
#include <audio.h>  // DAC output
#include <codec.h>  // CODEC interface
#include <vsNand.h>
#include <mappertiny.h>
#include <usb.h>
  
#include <dev1000.h>
extern s_int16 __y btmp[64 /* BASS_SAMPLE_BLOCK */ ];
  
#define DT_LANGUAGES 0
#define DT_VENDOR 1
#define DT_MODEL 2
#define DT_SERIAL 3
#define DT_DEVICE 4
#define DT_CONFIGURATION 5
  
#define VENDOR_NAME_LENGTH 7
const u_int16 myVendorNameStr[] = { 
    ((VENDOR_NAME_LENGTH * 2 + 2) << 8) | 0x03, 'V' << 8, 'l' << 8, 's' << 8,
    'i' << 8, 'F' << 8, 'i' << 8, 'n' << 8
};

 
#define MODEL_NAME_LENGTH 8
const u_int16 myModelNameStr[] = { 
    ((MODEL_NAME_LENGTH * 2 + 2) << 8) | 0x03, 'S' << 8, 'P' << 8, 'I' << 8,
    'A' << 8, 'u' << 8, 'd' << 8, 'i' << 8, 'o' << 8
};

 
#define SERIAL_NUMBER_LENGTH 12
  u_int16 mySerialNumberStr[] =
{
  ((SERIAL_NUMBER_LENGTH * 2 + 2) << 8) | 0x03, '0' << 8, // You should put
                                                            // your own serial
                                                            // number here
    '0' << 8, '0' << 8, '0' << 8, '0' << 8, '0' << 8, '0' << 8, '0' << 8, '0' << 8, '0' << 8, '0' << 8, '1' << 8, // Serial 
                                                                                                                            // number 
                                                                                                                            // should 
                                                                                                                            // be 
                                                                                                                            // unique 
                                                                                                                            // for 
                                                                                                                            // each 
                                                                                                                            // unit
};

 
#ifdef AUDIO_PLUS_MASS_STORAGE
// This is the new Device Descriptor. See the USB specification! 
const u_int16 myDeviceDescriptor[] = "\p"  "\x12"  // Length
  "\x01"  // Type (Device Descriptor)
  "\x10"  // LO(bcd USB Specification version) (.10)
  "\x01"  // HI(bcd USB Specification version) (1.)
  "\x00"  // Device Class (will be specified in configuration descriptor)
  "\x00"  // Device Subclass
  "\x00"  // Device Protocol
  "\x40"  // Endpoint 0 size (64 bytes)
   "\xfb"  // LO(Vendor ID) (0x19fb=VLSI Solution Oy)
  "\x19"  // HI(Vendor ID)
   "\xe3"  // LO(Product ID) (0xeee0 - 0xeeef : VLSI Customer Testing)
  "\xee"  // HI(Product ID) (customers can request an ID from VLSI)
   "\x00"  // LO(Release Number)
  "\x00"  // HI(Release Number)
  "\x01"  // Index of Vendor (manufacturer) name string
  "\x02"  // Index of Product (model) name string (most shown by windows)
  "\x03"  // Index of serial number string
  "\x01"  // Number of configurations (1)
  ;
 
#define CONFIG_DESC_SIZE 123
  u_int16 myConfigurationDescriptor[] = "\p"  
  // ______Configuration_descriptor____ at offset 0
  "\x09"  // Length: 9 bytes // (9)
  "\x02"  // Descriptor type: Configuration // (2)
  "\x7b"  // LO(TotalLength) (123)
  "\x00"  // HI(TotalLength) (0)
  "\x03"  // Number of Interfaces (3)
  "\x01"  // bConfigurationValue (1)
  "\x00"  // iConfiguration (0)
  "\x80"  // Attributes (128)
  "\x32"  // Max. Power 100mA (50)
   
  // ______Interface_descriptor____ at offset 9
  "\x09"  // Length: 9 bytes // (9)
  "\x04"  // Descriptor type: Interface // (4)
  "\x00"  // Interface number // (1)
  "\x00"  // Alternate setting // (0)
  "\x00"  // Number of endpoints // (0)
  "\x01"  // Interface Class: Audio // (1)
  "\x01"  // Interface Subclass: Audio Control // (1)
  "\x00"  // Interface Protocol // (0)
  "\x00"  // String index // (0)
  
  // ______Audio Control Class Interface_descriptor____ at offset 18
  "\x09"  // Length: 9 bytes // (9)
  "\x24"  // Descriptor type: Audio Control Class Interface // (36)
  "\x01"  // Subtype Header // (1)
  "\x00"  // (0)
  "\x01"  // (1)
  "\x1E"  // (30)
  "\x00"  // (0)
  "\x01"  // (1)
  "\x01"  // (1)
  
  // ______Audio Control Class Interface_descriptor____ at offset 27
  "\x0C"  // Length: 12 bytes // (12)
  "\x24"  // Descriptor type: Audio Control Class Interface // (36)
  "\x02"  // Subtype Input Terminal // (2)
  "\x01"  // (1)
  "\x01"  // (1)
  "\x01"  // (1)
  "\x00"  // (0)
  "\x02"  // (2)
  "\x00"  // (0)
  "\x00"  // (0)
  "\x00"  // (0)
  "\x00"  // (0)
  
  // ______Audio Control Class Interface_descriptor____ at offset 39
  "\x09"  // Length: 9 bytes // (9)
  "\x24"  // Descriptor type: Audio Control Class Interface // (36)
  "\x03"  // Subtype Output Terminal // (3)
  "\x02"  // (2)
  "\x04"  // (4)
  "\x03"  // (3)
  "\x00"  // (0)
  "\x01"  // (1)
  "\x00"  // (0)
  
  // ______Interface_descriptor____ at offset 48
  "\x09"  // Length: 9 bytes // (9)
  "\x04"  // Descriptor type: Interface // (4)
  "\x01"  // Interface number // (1)
  "\x00"  // Alternate setting // (0)
  "\x00"  // Number of endpoints // (0)
  "\x01"  // Interface Class: Audio // (1)
  "\x02"  // Interface Subclass: Audio Streaming // (2)
  "\x00"  // Interface Protocol // (0)
  "\x00"  // String index // (0)
  
  // ______Interface_descriptor____ at offset 57
  "\x09"  // Length: 9 bytes // (9)
  "\x04"  // Descriptor type: Interface // (4)
  "\x01"  // Interface number // (1)
  "\x01"  // Alternate setting // (1)
  "\x01"  // Number of endpoints // (1)
  "\x01"  // Interface Class: Audio // (1)
  "\x02"  // Interface Subclass: Audio Streaming // (2)
  "\x00"  // Interface Protocol // (0)
  "\x00"  // String index // (0)
  
  // ______Audio Streaming Class Interface_descriptor____ at offset 66
  "\x07"  // Length: 7 bytes // (7)
  "\x24"  // Descriptor type: Audio Streaming Class Interface // (36)
  "\x01"  // Subtype // (1)
  "\x01"  // (1)
  "\x0C"  // (12)
  "\x01"  // (1)
  "\x00"  // (0)
  
  // ______Audio Streaming Class Interface_descriptor____ at offset 73
  "\x0B"  // Length: 11 bytes // (11)
  "\x24"  // Descriptor type: Audio Streaming Class Interface // (36)
  "\x02"  // Subtype // (2)
  "\x01"  // (1)
  "\x02"  // (2)
  "\x02"  // (2)
  "\x10"  // (16)
  "\x01"  // (1)
  "\x44"  // (68)
  "\xAC"  // (172)
  "\x00"  // (0)
  
  // ______Endpoint_descriptor____ at offset 84
  "\x09"  // Length: 9 bytes // (9)
  "\x05"  // Descriptor type: Endpoint // (5)
  "\x01"  // Endpoint Address // (1)
  "\x09"  // Attributes.TransferType Isochronous(1) // (9)
  "\xFF"  // EP Size LSB // (255)
  "\x03"  // EP Size MSB (total 1023 bytes) // (3)
  "\x01"  // Polling Interval ms // (1)
  "\x00"  // Refresh // (0)
  "\x00"  // Sync Address // (0)
  
  // ______Class Endpoint_descriptor____ at offset 93
  "\x07"  // Length: 7 bytes // (7)
  "\x25"  // Descriptor type: Class Endpoint // (37)
  "\x01"  // Subtype // (1)
  "\x00"  // (0)
  "\x00"  // (0)
  "\x00"  // (0)
  "\x00"  // (0)
   
  // ______Interface_descriptor____ at offset 9
  "\x09"  // Length: 9 bytes // (9)
  "\x04"  // Descriptor type: Interface // (4)
  "\x02"  // Interface number // (2)
  "\x00"  // Alternate setting // (0)
  "\x02"  // Number of endpoints // (2)
  "\x08"  // Interface Class: Mass Storage // (8)
  "\x06"  // Interface Subclass: // (6)
  "\x50"  // Interface Protocol // (80)
  "\x00"  // String index // (0)
  
  // ______Endpoint_descriptor____ at offset 18
  "\x07"  // Length: 7 bytes // (7)
  "\x05"  // Descriptor type: Endpoint // (5)
  "\x82"  // Endpoint Address // (130)
  "\x02"  // Attributes.TransferType Bulk(2) // (2)
  "\x40"  // EP Size LSB // (64)
  "\x00"  // EP Size MSB (total 64 bytes) // (0)
  "\x10"  // Polling Interval ms // (16)
  
  // ______Endpoint_descriptor____ at offset 25
  "\x07"  // Length: 7 bytes // (7)
  "\x05"  // Descriptor type: Endpoint // (5)
  "\x03"  // Endpoint Address // (3)
  "\x02"  // Attributes.TransferType Bulk(2) // (2)
  "\x40"  // EP Size LSB // (64)
  "\x00"  // EP Size MSB (total 64 bytes) // (0)
  "\x10"  // Polling Interval ms // (16)
  // end of descriptor at offset 31
   
  /*  u_int16 myDescriptor[] = "\p"  // ______Configuration_descriptor____ at 
   * offset 0 "\x09" // Length: 9 bytes // (9) "\x02" // Descriptor type:
   * Configuration // (2) "\x64" // (100) "\x00" // (0) "\x02" // (2)
   * "\x01" // (1) "\x00" // (0) "\x80" // (128) "\x32" // (50) */ 
  ;

#endif  /*AUDIO_PLUS_MASS_STORAGE*/
    
// When a USB setup packet is received, install our descriptors 
// and then proceed to the ROM function RealDecodeSetupPacket.
void RealInitUSBDescriptors (u_int16 initDescriptors);
void MyInitUSBDescriptors (u_int16 initDescriptors)
{
  RealInitUSBDescriptors (1);  // ROM set descriptors for mass storage
  USB.descriptorTable[DT_VENDOR] = myVendorNameStr;
  USB.descriptorTable[DT_MODEL] = myModelNameStr;
  USB.descriptorTable[DT_SERIAL] = mySerialNumberStr;
  
#ifdef AUDIO_PLUS_MASS_STORAGE
    /* Win2000 is confused about composite device with both mass storage and
     * audio device. It just stops talking to the mass storage after the first 
     * Request Sense. Win7 is only slightly better. It seems to lock up if
     * audio and mass storage are used at the same time. */ 
    USB.descriptorTable[DT_DEVICE] = myDeviceDescriptor;
  USB.descriptorTable[DT_CONFIGURATION] = myConfigurationDescriptor;
  USB.configurationDescriptorSize = CONFIG_DESC_SIZE;
  
#endif  /*  */
}  

#if 0
// this can be helpful in making a serial number
const u_int16 bHexChar16[] = {  // swapped Unicode hex characters
  0x3000, 0x3100, 0x3200, 0x3300, 0x3400, 0x3500, 0x3600, 0x3700, 0x3800,
    0x3900, 0x4100, 0x4200, 0x4300, 0x4400, 0x4400, 0x4500 
};

My  void MakeSerialNumber (u_int32 newSerialNumber)
{
  u_int16 i;
  u_int32 mySerialNumber = newSerialNumber;
  
    // Put unique serial number to serial number descriptor
    for (i = 5; i < 13; i++)
  {
    mySerialNumberStr[i] = bHexChar16[mySerialNumber >> 28];
    mySerialNumber <<= 4;
  }
}


#endif  /*  */
extern struct FsNandPhys fsNandPhys;
extern struct FsPhysical *ph;
extern struct FsMapper *map;
extern struct Codec *cod;
extern struct CodecServices cs;

//extern u_int16 codecVorbis[];
  
/* cache info */ 
  u_int16 blockPresent;
u_int16 blockAddress[CACHE_BLOCKS];
s_int16 lastFoundBlock = -1;
u_int16 shouldFlush = 0;
  
// Do we want to get debug screen output? It's available if the code
// is loaded with vs3emu (build script) with RS-232 cable.
   
//macro to set SPI to MASTER; 8BIT; FSYNC Idle => xCS high
#define SPI_MASTER_8BIT_CSHI   PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE1
  
//macro to set SPI to MASTER; 8BIT; FSYNC not Idle => xCS low
#define SPI_MASTER_8BIT_CSLO   PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE0
  
//macro to set SPI to MASTER; 16BIT; FSYNC not Idle => xCS low
#define SPI_MASTER_16BIT_CSLO  PERIP(SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN16 | SPI_CF_FSIDLE0
void SingleCycleCommand (u_int16 cmd)
{
  SPI_MASTER_8BIT_CSHI;
  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive (cmd);
  SPI_MASTER_8BIT_CSHI;
}  

/// Wait for not_busy (status[0] = 0) and return status
  u_int16 SpiWaitStatus (void)
{
  u_int16 status;
  SPI_MASTER_8BIT_CSHI;
  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive (SPI_EEPROM_COMMAND_READ_STATUS_REGISTER);
  
  do
  {
    status = SpiSendReceive (0);
    if (PERIP (USB_STATUS) & USB_STF_BUS_RESET)
    {
      USBHandler ();
      SPI_MASTER_8BIT_CSHI;
      return -1; /* USB HAS BEEN RESET */
    }
  }
  while (status & 0x01);
  ;  // Wait until chip is ready or return -1 if USB bus reset
  SPI_MASTER_8BIT_CSHI;
   return status;
}

  void EeUnprotect ()
{
  SingleCycleCommand (SPI_EEPROM_COMMAND_WRITE_ENABLE);
  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive (SPI_EEPROM_COMMAND_WRITE_STATUS_REGISTER);
  SpiSendReceive (0x02); // Sector Protections Off
  SPI_MASTER_8BIT_CSHI;
  SpiWaitStatus ();
  SingleCycleCommand (SPI_EEPROM_COMMAND_WRITE_ENABLE);
}  void EePutReadBlockAddress (register u_int16 blockn)
{
  SPI_MASTER_8BIT_CSLO;
  SpiSendReceive (SPI_EEPROM_COMMAND_READ);
  SpiSendReceive (blockn >> 7);  // Address[23:16] = blockn[14:7]
  SpiSendReceive ((blockn << 1) & 0xff);  // Address[15:8] = blockn[6:0]0
  SpiSendReceive (0); // Address[7:0] = 00000000
  SPI_MASTER_16BIT_CSLO;
}  

// Check is a 4K block completely blank
  u_int16 EeIsBlockErased (u_int16 blockn)
{
  SpiWaitStatus ();
  EePutReadBlockAddress (blockn);
  
  {
    register u_int16 n;
    for (n = 0; n < 2048; n++)
    {
      if (SpiSendReceive (0) != 0xffff)
      {
        SPI_MASTER_8BIT_CSHI;
        return 0;
      }
    }
    SPI_MASTER_8BIT_CSHI;
    return 1;
  }
}

 s_int16 EeProgram4K (u_int16 blockn, __y u_int16 * dptr)
{
   PERIP (USB_EP_ST3) |= (0x0001);  // Force NAK on EP3 (perhaps not
                                      // needed?)
  
#ifndef SPI_EEPROM
    if (!EeIsBlockErased (blockn))
  { // don't erase if not needed
    // Erase 4K sector
    SingleCycleCommand (SPI_EEPROM_COMMAND_WRITE_ENABLE);
    SingleCycleCommand (SPI_EEPROM_COMMAND_CLEAR_ERROR_FLAGS);
    EeUnprotect ();
    SPI_MASTER_8BIT_CSLO;
    SpiSendReceive (SPI_EEPROM_COMMAND_ERASE_4K_SECTOR);
    SpiSendReceive (blockn >> 7);  // Address[23:16] = blockn[14:7]
    SpiSendReceive ((blockn << 1) & 0xff);  // Address[15:8] = blockn[6:0]0
    SpiSendReceive (0); // Address[7:0] = 00000000
    SPI_MASTER_8BIT_CSHI;
  }
  
#endif  /*  */
    if (SpiWaitStatus () == -1)
    return -1;  /* USB HAS BEEN RESET */
  
    // Write 8 512-byte sectors
  {
    u_int16 i;
    for (i = 0; i < 8; i++)
    {
       
        // Put first page (256 bytes) of sector.
        EeUnprotect ();
      SPI_MASTER_8BIT_CSLO;
      SpiSendReceive (SPI_EEPROM_COMMAND_WRITE);
      SpiSendReceive (blockn >> 7);  // Address[23:16] = blockn[14:7]
      SpiSendReceive ((blockn << 1) & 0xff);  // Address[15:8] = blockn[6:0]0
      SpiSendReceive (0); // Address[7:0] = 00000000
      SPI_MASTER_16BIT_CSLO;
      
      {
        u_int16 n;
        for (n = 0; n < 128; n++)
        {
          
#if USE_INVERTED_DISK_DATA
            SpiSendReceive (~(*dptr++));
          
#else /*  */
            SpiSendReceive ((*dptr++));
          
#endif  /*  */
        }
      }
      SPI_MASTER_8BIT_CSHI;
      if (SpiWaitStatus () == -1)
        return -1;  /* USB HAS BEEN RESET */
       
        // Put second page (256 bytes) of sector.
        EeUnprotect ();
      SPI_MASTER_8BIT_CSLO;
      SpiSendReceive (SPI_EEPROM_COMMAND_WRITE);
      SpiSendReceive (blockn >> 7);  // Address[23:16] = blockn[14:7]
      SpiSendReceive (((blockn << 1) + 1) & 0xff);  // Address[15:8] =
                                                    // blockn[6:0]1
      SpiSendReceive (0); // Address[7:0] = 00000000
      SPI_MASTER_16BIT_CSLO;
      
      {
        u_int16 n;
        for (n = 0; n < 128; n++)
        {
          
#if USE_INVERTED_DISK_DATA
            SpiSendReceive (~(*dptr++));
          
#else /*  */
            SpiSendReceive ((*dptr++));
          
#endif  /*  */
        }
      }
      SPI_MASTER_8BIT_CSHI;
      if (SpiWaitStatus () == -1)
        return -1;  /* USB HAS BEEN RESET */
      blockn++;
    }
  }
  PERIP (USB_EP_ST3) &= ~(0x0001); // Un-Force NAK on EP3
  return 0;
 }

   
// Block Read for SPI EEPROMS with 24-bit address e.g. up to 16MB 
  u_int16 EeReadBlock (u_int16 blockn, u_int16 * dptr)
{
  SpiWaitStatus ();
   EePutReadBlockAddress (blockn);
  
  {
    int n;
    for (n = 0; n < 256; n++)
    {
      
#if USE_INVERTED_DISK_DATA
        *dptr++ = ~SpiSendReceive (0);
      
#else /*  */
        *dptr++ = SpiSendReceive (0);
      
#endif  /*  */
    }
  }
  SPI_MASTER_8BIT_CSHI;
  return 0;
}

 
// Returns 1 if block differs from data, 0 if block is the same
  u_int16 EeCompareBlock (u_int16 blockn, u_int16 * dptr)
{
  SpiWaitStatus ();
   EePutReadBlockAddress (blockn);
  
  {
    int n;
    for (n = 0; n < 256; n++)
    {
       
#if USE_INVERTED_DISK_DATA      
        if ((*dptr++) != (~SpiSendReceive (0)))
        
#else /*  */
        if ((*dptr++) != (SpiSendReceive (0)))
        
#endif  /*  */
      {
        SPI_MASTER_8BIT_CSHI;
        return 1;
      }
    }
  }
  SPI_MASTER_8BIT_CSHI;
  return 0;
}

 u_int16 EeRead4KSectorYToWorkspace (u_int16 blockn)
{
  register __y u_int16 *dptr;
  dptr = WORKSPACE;
  SpiWaitStatus ();
  blockn &= 0xfff8;  // always point to first block of 4k sector
   EePutReadBlockAddress (blockn);
  
  {
    int n;
    for (n = 0; n < 2048; n++)
    {
      
#if USE_INVERTED_DISK_DATA      
        *dptr++ = ~SpiSendReceive (0);
      
#else /*  */
        *dptr++ = SpiSendReceive (0);
      
#endif  /*  */
    }
  }
  SPI_MASTER_8BIT_CSHI;
  return 0;
}

 void InitSpi (u_int16 clockDivider)
{
  SPI_MASTER_8BIT_CSHI;
  PERIP (SPI0_FSYNC) = 0;
  PERIP (SPI0_CLKCONFIG) = SPI_CC_CLKDIV * (clockDivider - 1);
  PERIP (GPIO1_MODE) |= 0x1f;  /* enable SPI pins */
}   __y u_int16 * FindCachedBlock (u_int16 blockNumber)
{
  register int i;
  lastFoundBlock = -1;
  for (i = 0; i < CACHE_BLOCKS; i++)
  {
    if ((blockPresent & 1 /* was L */  << i)
         && (blockAddress[i] == blockNumber))
    {
      lastFoundBlock = i;
      return mallocAreaY + 256 * i;
    }
  }
  return NULL;
}

  u_int16 WriteContinuous4K ()
{
  u_int16 i, k;
  for (i = 0; i < CACHE_BLOCKS - 7; i++)
  { // for all cached blocks...
    if ((blockPresent & 1 /* was L */  << i)
        && ((blockAddress[i] & 0x0007) == 0))
    {
      
        // cached block i contains first 512 of 4K
        for (k = 1; k < 8; k++)
      {
        if (blockAddress[i + k] != blockAddress[i] + k)
          goto ohi;
      }
      if (-1 != EeProgram4K (blockAddress[i], mallocAreaY + 256 * i))
      {
        for (k = 0; k < 8; k++)
        {
          blockPresent &= ~(1 /* was L */  << (i + k));
        }
      }
      else
      {
        return 0;  /* USB HAS BEEN RESET */
      }
      return 1;
    }
  ohi:
    {
    }
  }
  return 0;
}

 __y u_int16 * GetEmptyBlock (u_int16 blockNumber)
{
  register int i;
  for (i = 0; i < CACHE_BLOCKS; i++)
  {
    if (!(blockPresent & 1 /* was L */  << i))
    {
      blockPresent |= 1 /* was L */  << i;
      blockAddress[i] = blockNumber;
      return mallocAreaY + 256 * i;
    }
  }
  return NULL;
}

   struct FsMapper *FsMapSpiFlashCreate (struct FsPhysical *physical,
                                             u_int16 cacheSize);
s_int16 FsMapSpiFlashRead (struct FsMapper *map, u_int32 firstBlock,
                            u_int16 blocks, u_int16 * data);
s_int16 FsMapSpiFlashWrite (struct FsMapper *map, u_int32 firstBlock,
                             u_int16 blocks, u_int16 * data);
s_int16 FsMapSpiFlashFlush (struct FsMapper *map, u_int16 hard);
  struct FsMapper spiFlashMapper = { 0x010c, /* version */ 
    256, /* blocksize */ 
    LOGICAL_DISK_BLOCKS, /* blocks */ 
    0, /* cacheBlocks */ 
    FsMapSpiFlashCreate, FsMapFlNullOk,  // RamMapperDelete,
  FsMapSpiFlashRead, FsMapSpiFlashWrite, NULL,  // FsMapFlNullOk,//RamMapperFree,
  FsMapSpiFlashFlush, // RamMapperFlush,
  NULL /* no physical */  
};

struct FsMapper *FsMapSpiFlashCreate (struct FsPhysical *physical,
                                       u_int16 cacheSize)
{
   InitSpi (SPI_CLOCK_DIVIDER);
  blockPresent = 0;
  shouldFlush = 0;
  
#if 1
/* RDID: Command / manufacturer ID / memory type / memory density */ 
    
    /* Determine size from the RDID sequence */ 
  {
    u_int16 mf = 0, status = 0;
     SPI_MASTER_8BIT_CSHI;
    SPI_MASTER_8BIT_CSLO;
    SpiSendReceive (0x9f);
    mf = SpiSendReceive (0);
    SPI_MASTER_16BIT_CSLO;
    status = SpiSendReceive (0);
    SPI_MASTER_8BIT_CSHI;
    
#ifdef UART_CONTROL
      if (mf == 0xc2)
    { /* Macronix */
      putstrp ("\pMacronix\n");
    }
    else
    {
      putstrp ("\pUnknown manufacturer\n");
    }
    if ((status & 0xff) == 21)
    {
      putstrp ("\p2MB\n");
    }
    else if ((status & 0xff) == 24)
    {
      putstrp ("\p16MB\n");
    }
    
#endif  /*  */
      if ((status & 0xff))
    {
      spiFlashMapper.blocks = (1L << ((status & 0xff) - 9)) - RESERVED_BLOCKS;
    }
    else
    {
      
        /* undefined size */ 
    }
  }
  
#endif  /*  */
#if 0
/*REMS: Command+2dummy+1addr / manufacturer ID / device ID */ 
    /* Determine size from the REMS sequence -- just appears to work with
     * Macronix. */ 
  {
    u_int16 status = 0;
     SPI_MASTER_8BIT_CSHI;
    SPI_MASTER_16BIT_CSLO;
    SpiSendReceive (0x9000);
    SpiSendReceive (0);
    status = SpiSendReceive (0);
    SPI_MASTER_8BIT_CSHI;
    
#ifdef UART_CONTROL
      if ((status & 0xff00) == 0xc200)
    { /* Macronix */
      putstrp ("\pMacronix\n");
    }
    else
    {
      putstrp ("\pUnknown manufacturer\n");
    }
    if ((status & 0xff) == 20)
    {
      putstrp ("\p2MB\n");
    }
    else if ((status & 0xff) == 23)
    {
      putstrp ("\p16MB\n");
    }
    
#endif  /*  */
      if ((status & 0xff))
    {
      spiFlashMapper.blocks = (1L << ((status & 0xff) - 8)) - RESERVED_BLOCKS;
    }
  }
  
#endif  /*  */
    return &spiFlashMapper;
}

   s_int16 FsMapSpiFlashRead (struct FsMapper * map, u_int32 firstBlock,
                                  u_int16 blocks, u_int16 * data)
{
  register s_int16 bl = 0;
   if (shouldFlush)
    return 0;
   
//PERIP(GPIO0_ODATA) ^= 0x0020;
    firstBlock += RESERVED_BLOCKS;
  while (bl < blocks)
  {
    __y u_int16 * source = FindCachedBlock (firstBlock);
    if (source)
    {
      memcpyYX (data, source, 256);
    }
    else
    {
      EeReadBlock (firstBlock, data);
      
        // memset(data, 0, 256);
    }
    data += 256;
    firstBlock++;
    bl++;
  }
  return bl;
}

  s_int16 FsMapSpiFlashWrite (struct FsMapper * map, u_int32 firstBlock,
                                 u_int16 blocks, u_int16 * data)
{
  s_int16 bl = 0;
   firstBlock += RESERVED_BLOCKS;
   if (shouldFlush)
  {
    return 0;  // don't accept write while flushing
  }
  while (bl < blocks)
  {
    
      // Is the block to be written different than data already in EEPROM?
      if (EeCompareBlock (firstBlock, data))
    {
      __y u_int16 * target = FindCachedBlock (firstBlock);
      if (target)
      {
      }
      else
      {
        target = GetEmptyBlock (firstBlock);
      }
      if (!target)
      { // cache is full
        // must do a cache flush to get cache space
        FsMapSpiFlashFlush (NULL, 1);
        target = GetEmptyBlock (firstBlock);
      }
      if (target)
      {
        memcpyXY (target, data, 256);
      }
      else
      {
        
          /* FATAL ERROR: NO CACHE SPACE. THIS NEVER HAPPENS. */ 
          while (1)
          ;
      }
      WriteContinuous4K ();
    }
    else
    {
      
        /*=lba; Redundant write skipped*/ 
    }
     if (PERIP (USB_STATUS) & USB_STF_BUS_RESET)
    {
      
        /* USB: Reset */ 
    }
    data += 256;
    firstBlock++;
    bl++;
  }
  return bl;
}

  s_int16 FsMapSpiFlashFlush (struct FsMapper * map, u_int16 hard)
{
  u_int16 i, j, lba;
  u_int16 __y * dptr;
  u_int16 newBlockPresent;
   if (shouldFlush > 1)
    return 0;
  shouldFlush = 2; // flushing
  for (i = 0; i < CACHE_BLOCKS; i++)
  {
    if (blockPresent & (1 /* was L */  << i))
    {
      lba = blockAddress[i] & 0xfff8;
      EeRead4KSectorYToWorkspace (lba);
      newBlockPresent = blockPresent;
      for (j = 0; j < 8; j++)
      {
        if (dptr = FindCachedBlock (lba + j))
        {
          memcpyYY (WORKSPACE + (256 * j), dptr, 256);
          newBlockPresent &= ~(1 /* was L */  << lastFoundBlock);
         }
        else
        {
        }
      }
      if (-1 != EeProgram4K (lba, WORKSPACE))
      {
        blockPresent = newBlockPresent;
        shouldFlush = 0;
      }
      else
      {
        shouldFlush = 1;
        return 0;  /* USB HAS BEEN RESET */
      }
    }
  }
  shouldFlush = 0;
  return 0;
 }

 
#if defined(PATCH_EJECT) || defined(PATCH_SENSE)
#include <usblowlib.h>
#include <scsi.h>
int startUnit = 1;
 auto u_int16 NewDiskProtocolCommand (register __i2 u_int16 * cmd)
{
  static u_int16 checksumSave[5];
   
    /* Note: command length is in hi byte of cmd[0], so bytes of the SCSI
     * commands are: hi lo word 0 len scsi byte 0 (command) word 1 1 2 word 
     * 2 3 4 word 3 5 6 word 4 7 8 READ_10 LBA: 2-5 */ 
  __y int c = (cmd[OPERATION_CODE] & 0xff);
  
#if defined(PATCH_EJECT)
    if (c == SCSI_START_STOP_UNIT)
  {
    
      /* 0:OP 1:imm0 2:res 3:res 4:power7-4 res3-2 loej1 start0 5:control */ 
      startUnit = cmd[4] & 1;
  }
  else if (startUnit == 0)
  {
    
      /* The next command after STOP UNIT will not be serviced fully because
       * we will turn ourselves off. */ 
      startUnit = 2;
  }
  
#endif  /*  */
#if defined(PATCH_SENSE)
    if (c == SCSI_REQUEST_SENSE)
  {
    
      /* patch a bug -- sense was written to the wrong buffer */ 
      SCSI.DataBuffer[0] = 0x7000;
    SCSI.DataOutBuffer = SCSI.DataBuffer;
    SCSI.DataOutSize = 18; /* Windows requires 18. */
    SCSI.State = SCSI_DATA_TO_HOST;
    return -1; /* done with the command */
  }
  
#endif  /*  */
    return cmd[OPERATION_CODE]; /* perform the command */
}


#endif  /*  */
auto void MyMassStorage (void)
{
   
#if defined(PATCH_EJECT) || defined(PATCH_SENSE)
    /* fixes SCSI_READ_10 residue problem -- mainly for Linux compatibility,
     * does not fix LBAB, but is shorter.. */ 
    SetHookFunction ((u_int16) MSCPacketFromPC, PatchDiskProtocolCommandDE);
  
    // SetHookFunction((u_int16)MSCPacketFromPC, PatchDiskProtocolCommandC);
#endif  /*  */
    voltages[voltCoreUSB] = 31; // 30:ok
  voltages[voltIoUSB] = 31; // set maximum IO voltage (about 3.6V)
  PowerSetVoltages (&voltages[voltCoreUSB]);
  BusyWait10 ();
  LoadCheck (NULL, 1); /* Set 48 MHz Clock */
  SetRate (44100U);
   PERIP (SCI_STATUS) &= ~SCISTF_ANADRV_PDOWN;
  
  {
    register int i;
    for (i = 0; i < 2048; i += 32)
    {
      memset (tmpBuf, -0x0fff, 2 * 16);
      AudioOutputSamples (tmpBuf, 16);
      memset (tmpBuf, 0x0fff, 2 * 16);
      AudioOutputSamples (tmpBuf, 16);
    }
  }
    SetHookFunction ((u_int16) InitUSBDescriptors, MyInitUSBDescriptors);
  InitUSB (USB_MASS_STORAGE);
  USB.lastSofTime = ReadTimeCount ();
   while (1)
  {
    USBHandler ();
    if (shouldFlush)
    {
      FsMapSpiFlashFlush (NULL, 1);
    }
    if (USBWantsSuspend () 
#if 1 /* Allow detach detection even when we have not yet been configured */
         ||(USB.lastSofTime
            && ReadTimeCount () - USB.lastSofTime > 2 * TIMER_TICKS) 
#endif  /*  */
      )
    {
      if (USBIsDetached ())
      {
        break;
      }
      else
      {
        
          /* suspend? */ 
      }
    }
     if (AudioBufFill () < 32)
    {
      memset (tmpBuf, 0, sizeof (tmpBuf));
      AudioOutputSamples (tmpBuf, sizeof (tmpBuf) / 2);
    }
    Sleep ();
     if (startUnit == 2)
    {
      
        /* Take us 'out of the bus' */ 
        PERIP (SCI_STATUS) &= ~SCISTF_USB_PULLUP_ENA;
      PERIP (USB_CONFIG) = 0x8000U;  /* Reset USB */
      map->Flush (map, 2); /* Flush content */
       if ((USEX (SCI_STATUS) & SCISTF_REGU_POWERBUT))
      {
        
          /* should initialize vectors and jump to rom code! */ 
          ResetIRAMVectors ();
        map = NULL;
        JAfterSpi ();
      }
      else
      {
        PowerOff ();
      }
    }
  }
   
#ifdef PATCH_SCSI_COMMAND
    /* Restore the default function */ 
    SetHookFunction ((u_int16) MSCPacketFromPC, RealMSCPacketFromPC);
  
#endif  /*  */
    hwSampleRate = 1;
  PERIP (SCI_STATUS) &= ~SCISTF_USB_PULLUP_ENA;
  PERIP (USB_CONFIG) = 0x8000U;
  map->Flush (map, 1);
  LoadCheck (NULL, 0);
  PowerSetVoltages (&voltages[voltCorePlayer]);
}

 
// FAT12 binary patch
  auto u_int16 Fat12OpenFile (register __c0 u_int16 n);
 
// Try to Load and execute a file
//Note: your program should set all hooks used by this program!
void Exec (u_int32 extension)
{
  static u_int32 allowedExtensions[] = { 0, 0 };
  allowedExtensions[0] = extension;
  
    // Initialize the filesystem for nand flash physical "just in case".
    // If filesystem has already been initialized,
    // then these 3 lines can be omitted:
    if (!map)
  {
    map = FsMapSpiFlashCreate (NULL, 0);
  }
  if (InitFileSystem () == 0)
  {
    
      /* Reinitialize the filing system */ 
      minifatInfo.supportedSuffixes = allowedExtensions;
    if (Fat12OpenFile (0) < 0)
    {
      if (ReadFile (mallocAreaX, 0, 0x2000))
      {
        BootFromX (mallocAreaX + 8); // Exit to new image 
        { // Exec failed.
          // BootFromX returns only in case of invalid prg file.
          // Program execution would be unsafe after failed BootFromX
          // so the system falls to playing an "Error tone".
          register u_int16 *g = (u_int16 *) g_yprev0; // sintest params
          *g++ = 4;
          *g++ = 44100;
          *g++ = 0x5050;
          *g++ = 120;
          *g++ = 200;
          SinTest ();  // Exit to SinTest: Play weird tone at low volume
      } }
    }
  }
}   

/* Patched suspend takes only a little more than 500mA */ 
void MyUSBSuspend (u_int16 timeOut)
{
  PERIP (SCI_STATUS) |= SCISTF_ANA_PDOWN | SCISTF_ANADRV_PDOWN;
  RealUSBSuspend (timeOut);
  PERIP (SCI_STATUS) &= ~(SCISTF_ANA_PDOWN | SCISTF_ANADRV_PDOWN);
}  void MyPowerOff (void)
{
  
    /* Because there is a large capacitor on CVDD, we have to make otherwise
     * certain that the voltage does not rise above the power-on-reset level
     * when power consumption drops before the regulators have been properly
     * shut down.  In this case we bring the SPI FLASH chip select low, so
     * that IOVDD is dropping quickly, and prevents restart by preventing the 
     * oscillator circuit from running. */ 
    PERIP (GPIO1_DDR) |= 1;
  PERIP (GPIO1_MODE) &= ~1;
  PERIP (GPIO1_CLEAR_MASK) = 1;  /* XCS down, possibly consumes IO power */
  
    /*  You may also need to draw IOVDD empty through RX pull-up */ 
    RealPowerOff ();
}  

#ifdef UART_CONTROL
/*
  put packed string
 */ 
void putstrp (register __i0 u_int16 * packedStr)
{
  while (1)
  {
    register int i = *packedStr++;
    if (i >> 8)
    {
      putch (i >> 8);
    }
    else
    {
      break;
    }
    if (i & 255)
    {
      putch (i);
    }
    else
    {
      break;
    }
  }
}


#endif  /*  */
void MyUserInterfaceIdleHook (void)
{
  if (uiTrigger)
  {
    uiTrigger = 0;
    
#if 0 /* No scanning during USB -- TODO: power button? */
      KeyScanNoUSB (0x1f);  /* with feature button */
    
#endif  /*  */
  }
}

  void main (void)
{
  
#ifdef EXTCLOCK
    extClock4KHz = EXTCLOCK / 4;  // 12000/4;
#endif  /*  */
  
#if 1 /* 2015-04-27 3.3V */
    voltages[voltIoSuspend] = voltages[voltIoPlayer] = voltages[voltIoUSB] = 27;
  PowerSetVoltages (&voltages[voltCorePlayer]);
  
#endif  /*  */
    if (!(PERIP (INT_ENABLEH) & INTF_DAC))
  {
    
      /* First boot? */ 
#if defined(USE_QUEUE) || defined(START_IN_FILE_MODE)
      playerMode = PLAYER_WAIT_FOR_COMMAND;
    
#else /*  */
      playerMode = PLAYER_CONTINUOUS_PLAY;
    
#endif  /*  */
      player.volume = 0;  // +24;
  }
  
#if 0
    uartByteSpeed = 9600 / 10;  // change the default rate here
#endif  /*  */
#ifdef GAPLESS
  clockX = 7;
  
#else /*  */
  clockX = 6;
  
#endif  /*  */
    InitAudio ();
  PERIP (UART_DIV) = UartDiv ();
   PERIP (INT_ENABLEL) |= INTF_RX | INTF_TIM0;
  PERIP (INT_ENABLEH) = INTF_DAC;
   PERIP (SCI_STATUS) &= ~SCISTF_USB_PULLUP_ENA;
  PERIP (USB_CONFIG) = 0x8000U;
   
    /* Version string */ 
    putstrp ("\pv0.72" 
#ifdef UART_CONTROL
             "-uart" 
#endif  /*  */
#ifdef GPIO_CONTROL
             "-gpio" 
#endif  /*  */
#ifdef USE_QUEUE
             "-queue" 
#endif  /*  */
#ifdef START_IN_FILE_MODE
             "-filemode" 
#endif  /*  */
#ifdef GAPLESS
             "-gapless" 
#endif  /*  */
             "\n" );
   
#if 0  
    PERIP (GPIO1_ODATA) |= LED1 | LED2;
  PERIP (GPIO1_DDR) |= LED1 | LED2;
  PERIP (GPIO1_MODE) &= ~(LED1 | LED2);
  
#endif  /*  */
    player.volumeOffset = 0;
  player.pauseOn = 0;
  
#ifdef POFFTIME
    player.offDelay = POFFTIME;
  
#endif  /*  */
    keyOld = KEY_POWER;
  keyOldTime = -32767;
   
//  SetHookFunction((u_int16)OpenFile, Fat12OpenFile);
    SetHookFunction ((u_int16) IdleHook, MyUserInterfaceIdleHook);
  SetHookFunction ((u_int16) PowerOff, MyPowerOff);
   
    // Use our SPI flash mapper as logical disk
    map = FsMapSpiFlashCreate (NULL, 0);
  
    // player.volume = 0;
    PlayerVolume ();
    
    /* configure GPIO0[0:7] to GPIO (inputs) */ 
    PERIP (GPIO0_MODE) &= 0xff00;
  
#if 0
    /* for 2FIN2 the power must be on for SD insertion to work */ 
    /* enable SD power -- 20150720 -- USE_POWERBUTTON fixed. */ 
    PERIP (GPIO0_DDR) = (PERIP (GPIO0_DDR) & ~(MMC_MISO)) | GPIO0_SD_POWER;
  PERIP (GPIO0_SET_MASK) = GPIO0_SD_POWER;
  
#endif  /*  */
    // PERIP(GPIO0_DDR) |= 0x0020;
    // PERIP(GPIO0_ODATA) ^= 0x0020;
    if (USBIsAttached ())
  {
    if ((PERIP (GPIO0_DDR) & (1 << 14)) == 0 && 
         (PERIP (GPIO0_IDATA) & (1 << 14)))
    {
      
        /* SD not inserted */ 
        
        /* goto USB mode */ 
#ifdef UART_CONTROL
        putstrp ("\pUSB Attach SPI\n");
      
#endif  /*UART_CONTROL*/
        MyMassStorage ();
      
#ifdef UART_CONTROL
        putstrp ("\pUSB Detach SPI\n");
      
#endif  /*UART_CONTROL*/
    }
    else
    {
      
        /* SD inserted -- load SD player to handle SD and mass storage. */ 
        
#ifdef UART_CONTROL
        putstrp ("\pSD\n");
      
#endif  /*UART_CONTROL*/
        ResetIRAMVectors ();  /* Doesn't set variables to defaults, only jump
                               * vectors */
      PERIP (SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE1;
      SpiLoad (SDMASSSTART + 4, 1 /* 24-bit address */ );
    }
  }
   
    // If EXECFILE.PRG is found on disk, run it. (Actually any *.PRG file)
    // (this is an easy way to do program development on the device, just
    // compile your own code to EXECFILE.PRG and drag-and-drop it to the
    // usb disk. It will replace the Ogg Vorbis player code that is below.
    Exec (FAT_MKID ('P', 'R', 'G'));
  
    // If EXECFILE.PRG was not found, continue below...
    
    /* If no USB, run in player mode only. */ 
    ResetIRAMVectors ();  /* Doesn't set variables to defaults, only jump
                           * vectors */
  PERIP (SPI0_CONFIG) = SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE1;
  if ((PERIP (GPIO0_DDR) & (1 << 14)) == 0 && 
       (PERIP (GPIO0_IDATA) & (1 << 14)))
  {
    
      /* SD not inserted */ 
      SpiLoad (SPIPLAYERSTART + 4, 1 /* 24-bit address */ );
  }
  else
  {
    
#ifdef UART_CONTROL
      putstrp ("\pSD\n");
    
#endif  /*UART_CONTROL*/
      /* SD inserted */ 
      SpiLoad (SDPLAYERSTART + 4, 1 /* 24-bit address */ );
  }
  while (1)
    ;
}


