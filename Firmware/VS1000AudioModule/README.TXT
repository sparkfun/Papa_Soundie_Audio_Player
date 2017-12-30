VS1000 Audio Module default firmware v0.72			20150720
-------------------------------------------------------------

Quickstart with VSIDE:
- Decompress the source code into your Solutions directory
- Start VSIDE and open VS1000AudioModule.solution
- Build all, and spiall.spi is created in the solution directory
  it is also copied to eeprom.img for easier programming
- Then program the module with the Prommer/Flasher utility
	"VS1000 SPI Flasher (24-bit) prepared image"
- If you don't have this version, you need to add the following entry to
  VSIDE\bin\prommer.index:

[vs1000_spi_flash_24bit_preparedimage]
Name                           = "VS1000 SPI Flasher (24-bit) prepared image"
TargetPlatform                 = "VS1000"
Description                    = "This VS1000 prommer can program SPI Flash memory chips with 24-bit address space. Communication is handled via UART cable. Image file has been prepared already by post-build step."
ImageName                      = "eeprom.img"
UseVs3Emu                      = "1"
PrommerBinary                  = "uniprom1k24.bin"
PrommerMemDesc                 = "prommer24.mem"

- One of the projects also create SDUPDATE.PRG for updates through SD card.
- Change settings from system.h (UART/GPIO, with or without WAV etc.)

Quickstart with Makefile:
- "make" (or "gmake") will compile and link the default firmware
- "make program" will program the default firmware to SPI FLASH
   if the FLASH already has a program, you must power on with SPI boot disabled

You can also use spi.bat to program the firmware:
> make spiall.spi prommer24.bin
> vs3emu -chip vs1000 -s 115200 -p 1 -l prommer24.bin -c run.cmd

Also see gpio-controlled player version.

Using SDUPDATE.PRG:
- SDUPDATE.PRG is a normal program, so it can do anything. The default
  SDUPDATE.PRG in the 0.4 version can update both the boot image
  (FIRMWARE.RAW) and the FAT disk content (CONTENT.RAW), depending on
  what files it finds on the SD card.
- Copy spiall.spi to FIRMWARE.RAW to update the firmware
- You can create CONTENT.RAW with for example the VS1000 image creator.
  Select SPI FLASH, no boot code, and set the Force FAT start to sector 0,
  so the resulting image will contain only the FAT.
- The SDUPDATE.PRG mechanism also exists in the 0.3 version,
  so you can update to 0.4 using uSD card.


Changes
=======
v0.72
- 20150720 with USE_POWERBUTTON, now requires the power button to be active
	for 0.2 sec before shutting down. (The 2FIN2 batch produces a
	pulse to PWRBTN when the uSD regulator is enabled.)

v0.70
- 20150113 The module datasheet updated.
- 20150107 USE_INFO includes 'i'nfo and 'I'nfo command to get the
	play length in seconds of a file without playing it. The same
	commands can be used to determine if a file exists.
- 20141117 USE_POWERBUTTON is now the default. You can turn off the module
	with a long press of the power button and turn the unit back on
	using the same button.
- 20141117 GAPLESS option added - selecting it from system.h will play files
	as back-to-back as possible. Only one-byte file number is printed when
	starting play and a single 'd' is sent when play ends (if UART mode).
- 20141117 A full version string with extra qualifiers such as
	-uart, -gpio, -queue, -filemode, -gapless is printed after startup.
- 20141115 Detects SPI FLASH size using the REMS sequence, so the size of
	the FLASH does not need to be correct when compiling the firmware.
	(If you want to reserve space for your own use, increase the
	 RESERVED_BLOCKS definition and use sectors from 64 upwards.)
- 20141113 Separated SD player and SD mass storage code. This frees up
	space so that more features can be used. The drawback is that uSD
	communication is reinitialized when switching between modes.
- 20141111 Fixed USE_MULTIPLE_BLOCK_WRITE in sdplayer.c . With it you
	can get around 500kB per second write speed to a modern uSD card.
	Note: This is not a default setting. You need to disable some other
	feature to fit it in (#undef USE_WAV in sdplayer.c or
	comment out //#define USE_WAV in system.h).
v0.65
- 20141028 Fixed 'P'lay by name when you have mixed OGG and WAV files.
	(Also gives file not found now if the file does not exist.)
v0.63
- 20140624 Added special button functions to gpioctrl.c .
   - prioritized buttons to select songs 1-3
   - STOP button
   - volume up and down buttons
   - power button is supported through USE_POWERBUTTON
v0.62
- 20140613 Added USE_POWERBUTTON to enable turn off using the PB input.
v0.61
- 20130923 Queueing option added. Enable both USE_QUEUE and DISABLE_USB
   from system.h to use it. DISABLE_USB removes the USB functionality from
   sdplayer.c, so that the queueing code can fit.
   When USE_QUEUE is active, the firmware starts in file mode. While in
   file mode and not playing, you can add files to the queue with
   "QFILENAMEOGG\n". If the file was found, the response will be
   "add FILENAME\n" or "ful\n" if there is no space in the queue,
   otherwise "nof FILENAME\n" (not found).
   Each queued file is added to the end of the queue.
   You can start playing the queue with "p0\n".
   In the play mode 'C' (cancel) only ends the play of the current file
   and continues with the next file in the queue. The same applies to
   'p' (previous) and 'n' (next) commands.

v0.60
- 20130118 MicroWav can now be enabled with USB (new libdev1000 required,
   included under lib/ with this VSIDE release).
	microwav does not support fast forward
0.51
- 20120621 Option to delete SDUPDATE.PRG from the SD card (see sdupdate.c)
- 20120621 Added a beep after programming is complete and you should remove the card.
 
0.5
- 20111209 VSIDE Solution of the UART version
- 20111209 mass storage functions changed. usbmass.c no longer
	provides audio + mass storage because win2000 and win7
	had some trouble with this kind of  composite device.
- 20111209 sdupdate.c: added a beep after programming is finished
- 20111025 sdplayer.c: removed GPIO_CONTROL stuff,
	added PatchPlayCurrentFile and PatchDiskProtocolCommandDE,
	fixes to uSD handling, MyMassStorage cleaned up.
	   spiplayer.c: removed GPIO_CONTROL stuff

0.4
- 20110429 QUIET_CANCEL define added (inactive) in uartctrl.c:
	when cancelled from pause state, lower volume so that
	portion of the old song will not be played.
- 20101223 Separated GPIO_CONTROL into its own package
- 20101213 Created GPIO_CONTROL version
- 20101115 Exec() crashed if not a valid FAT filesystem in SPI FLASH. (fixed)

0.31 20101115
- Changes to source package only. Makefile did not pass the MMCSTART
  define correctly in Linux and Windows.

0.3 20101027
- Added SD playback with UART control. The software now consists of
  three parts:
  1) usbmass, which is executed after power-on and handles USB Mass Storage
     for SPI FLASH, and loads spiplayer or sdplayer depeding on the SD/MMC
     insertion switch.
  2) spiplayer, which plays SPI FLASH content, loads sdplayer if SD/MMC
     insertion switch is triggered, and loads usbmass (using watchdog reset)
     if USB attachment is detected.
  3) sdplayer, which plays from SD/MMC, and also handles USB Mass Storage,
     and loads spiplayer if SD/MMC insertion switch is triggered.

0.2 20101011
- First version, UART control and playback from SPI FLASH.
