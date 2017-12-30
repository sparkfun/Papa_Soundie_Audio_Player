#ifndef SYSTEM_H
#define SYSTEM_H

//#define UART_CONTROL // Control the player through UART
#define GPIO_CONTROL // Control the player through GPIO, see more defines from gpioctrl.h
#define GPIO_NUMBERED_FILES // Control the player through GPIO, see more defines from gpioctrl.h


//#define SHOW_LONG_FILENAMES // Show long filenames in the "L" command
#define USE_WAV /* Play linear wav files using CodMicroWav from libdev1000. */
//#define START_IN_FILE_MODE

/* GAPLESS tries to speed up change from one Ogg file to the next,
   provided they have been encoded with the same parameters.
   UART responses during file change are reduced in this mode.
   Only one byte is sent for the file number when play starts
   and a single 'd' is sent when the play ends.
*/
//#define GAPLESS


#define DISABLE_USB //Disable USB code from sdplayer to give room for queue
//#define USE_QUEUE   //START_IN_FILE_MODE implied
#define USE_POWERBUTTON //Allow powerbutton to power off the module
#define USE_INFO /* include 'i'nfo and 'I'nfo commands */
//#define USE_TYPE /* include 'T'ype command */
//#define EXTCLOCK 13000 /*set clock if not 12MHz. usbmass.c sets clock at first boot */



#ifdef USE_WAV
#define OPENFILENAMED(a,b) OpenFileNamedSupported(a,b)
#else
#define OPENFILENAMED(a,b) OpenFileNamed(a,b)
#endif

#ifndef SPIPLAYERSTART
/*Note: do not change, these must match the values used in all
        post-build steps of the VSIDE projects. */
#define SPIPLAYERSTART  8192
#define SDPLAYERSTART   16384
#define SDMASSSTART     24576
#endif

/* The number of 512-byte blocks totally available in the SPI Flash chip.
   This is most important to be correct when you format the SPI FLASH
   mass storage drive through USB.
 */
#define CHIP_TOTAL_BLOCKS  32768 /*  32768 * 512 bytes = 16MB */
//#define CHIP_TOTAL_BLOCKS  8192 /*  8192 * 512 bytes = 4M (Winbond 25X32) */
//#define CHIP_TOTAL_BLOCKS  4096 /*  4096 * 512 bytes = 2M (Winbond 25X16) */
//#define CHIP_TOTAL_BLOCKS 512  /* 512 * 512 bytes = 256K (Winbond 25X20) */

/* Set aside some blocks for VS1000 boot code (and optional parameter data) .
   Note that this is different than in vs1000 button cell player and some
   other example codes.
 */
#define RESERVED_BLOCKS 64
#define LOGICAL_DISK_BLOCKS  (CHIP_TOTAL_BLOCKS-RESERVED_BLOCKS)

// storing the disk data inverted is optimal for the system.
// storing the disk data uninverted (as is) makes it easier to debug the SPI image
#define USE_INVERTED_DISK_DATA 1

#define GPIO0_PULLUPS   0x1b80 // Audio module has pull-ups in these pins

#ifdef ASM
#else
#define PLAYER_WAIT_FOR_COMMAND 0
#define PLAYER_CONTINUOUS_PLAY  1
extern int playerMode; // = PLAYER_CONTINUOUS_PLAY;

#ifdef GAPLESS
#ifndef COD_VORBIS_GENERAL_H
#define COD_VORBIS_GENERAL_H
#include <vstypes.h>
#include <codec.h>
#define COD_VORBIS_MAX_CHANNELS 2
struct CodecVorbis {
  struct Codec c;
  s_int16 used;
  u_int16 state;
  u_int16 audioChannels;
  u_int32 audioSampleRate;
  u_int32 bitRateMaximum;
  u_int32 bitRateNominal;
  u_int32 bitRateMinimum;
  u_int16 blockSize[2];
  const u_int16 __y *window[2];
  u_int16 codeBooks;
  struct CodeBook *codeBook;
  u_int16 floors;
  struct Floor1 __y *floor;
  u_int16 residues;
  struct Residue *residue;
  u_int16 maps;
  struct Map *map;
  u_int16 modes;
  struct Mode *mode;
  u_int16 modeNumber;
  s_int16 prevWinType;
  u_int32 audioBegins;
  s_int16 oldFrameDownshift[COD_VORBIS_MAX_CHANNELS];
  s_int16 oldFramePotentialDownshift[COD_VORBIS_MAX_CHANNELS];
};
#endif /* !COD_VORBIS_GENERAL_H */
extern struct CodecVorbis codecVorbis;
#endif/*GAPLESS*/


enum CodecError PlayWavOrOggFile(void);
enum CodecError PlayGaplessOggFile(void);


#define GPIO0_SD_POWER GPIO0_ALE
void CheckSd(void); //called by uart.c and gpio.c to check SD insert/remove

#include <vstypes.h>
/* generic support functions */
void putstrp(register __i0 u_int16 *packedStr);
unsigned short atou(register __i2 const char *s);

#if defined(UART_CONTROL) && defined(GPIO_CONTROL)
error_both_UART_CONTROL_and_GPIO_CONTROL_can_not_be_on_at_the_same_time();
#endif
#endif

#endif/*!SYSTEM_H*/
