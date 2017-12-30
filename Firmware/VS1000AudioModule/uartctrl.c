#include "system.h"

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

#include "uartctrl.h"
#include "system.h"

/* Note: this file does not check the UART_CONTROL define */

/* lower volume during song cancel, which is especially useful when
   changing songs from pause mode (waiting samples from the current
   song are not heard).
   Note: the QUIET_CANCEL define must be enabled in sdplayer.c and
         spiplayer.c (or system.h) as well.
 */
//#define QUIET_CANCEL

extern struct CodecServices cs;


/* Our own loadcheck which will use fixed 3.0x clock for player
   and 4.0x for USB. But as we don't have USB mode in this program,
   we would only need fixed clock. */
void UartLoadCheck(struct CodecServices *cs, s_int16 n) {
  if (cs == NULL) {
#if 1 /* USB is not serviced in spiplayer, but is in sdplayer. */
    if (n) {
        clockX = 8; /* USB requires 4.0x! */
        goto set;
    }
#endif
#ifdef GAPLESS
    if (clockX != 7) {
        clockX = 7; /* Otherwise 3.5x */
    set:
        SetRate(hwSampleRate);
    }
#else
    if (clockX != 6) {
        clockX = 6; /* Otherwise 3.0x */
    set:
        SetRate(hwSampleRate);
    }
#endif
    return;
  }
  /* cs is always non-NULL here */
  if (cs->gain != player.volumeOffset) {
    player.volumeOffset = cs->gain; /* vorbis-gain */
    PlayerVolume();
  }
}

#ifdef USE_QUEUE
extern s_int16 queued;
#endif

/*
  Only used during file play.
 */
void UartIdleHook(void) {
/*
  Controls:
 */
    if (UartFill()) {
        register int cmd = UartGetByte();
        if (cmd == 'f') {
            /* file mode */
            playerMode = PLAYER_WAIT_FOR_COMMAND;
        } else if (cmd == 'c') {
            /* continuous play mode */
            playerMode = PLAYER_CONTINUOUS_PLAY;
        } else if (cmd == 'C') {
            putch('c');
            goto cancelpauseoff;
            //cs.cancel = 1;
            //player.pauseOn = 0;
        } else if (cmd == '+') {
            KeyEventHandler(ke_volumeUp2);
            goto showvol;//putword(player.volume);
        } else if (cmd == '-') {
            KeyEventHandler(ke_volumeDown2);
        showvol:
            putword(player.volume);
        } else if (cmd == '=') {
            /* pause */
            player.pauseOn = 1;
            goto echo;//putch('=');
        } else if (cmd == '>') {
            /* play */
            player.pauseOn = 0;
            cs.fastForward = 1;
            goto echo;//putch('>');
        } else if (cmd == '»') {
            /* Fast forward */
            //if (cs.fastForward < 4)
        	++cs.fastForward;
            putch(cs.fastForward);
        } else if (cmd == 'n') {
            /* next */
            player.nextFile = player.currentFile + 1;
            putch('n');
            goto cancelpauseoff;
            //cs.cancel = 1;
            //player.pauseOn = 0;
        } else if (cmd == 'p') {
            /* previous */
            player.nextFile = player.currentFile - 1;
            putch('p');
            goto cancelpauseoff;
            //cs.cancel = 1;
            //player.pauseOn = 0;
        } else if (cmd == '?') {
            /* position + other info -- Note: will block. */
            putword(cs.playTimeSeconds>>16);
            putword(cs.playTimeSeconds);
            putch(cs.fileLeft / ((cs.fileSize >> 8)+1));
#ifdef USE_QUEUE
        } else if (cmd == 'R') {
            queued = 0;
            putch('R');
            goto cancelpauseoff;
#endif
        } else {
            /*unknown command*/
        echo:
            putch(cmd);
        }
    }

    if (uiTrigger) {
        uiTrigger = 0;

#if 0   /* This version does not allow keys to be used. */
        KeyScanNoUSB(0x1f); /* with feature button */
#endif
#ifdef USE_POWERBUTTON
        if (PERIP(SCI_STATUS) & SCISTF_REGU_POWERBUT) {
            /* In 2FIN2 switching on the uSD regulator affects powerbutton. */
            if (keyOld == (SCISTF_REGU_POWERBUT<<4)) {
        	if (++keyOldTime >= 3) { /* about 0.2 sec */
        	    PERIP(GPIO0_CLEAR_MASK) = GPIO0_SD_POWER;
        	    RealPowerOff();
        	} else {
        	    keyOldTime = 0;
        	}
            } else {
        	keyOld == (SCISTF_REGU_POWERBUT<<4);
            }
        } else {
            keyOld = 0;
        }
#endif
        CheckSd();
#ifndef DISABLE_USB
        if (USBIsAttached()) {
        cancelpauseoff:
            cs.cancel = 1;
            player.pauseOn = 0;
        }
#endif
    }
#ifdef QUIET_CANCEL
    if (cs.cancel) {
        volumeReg = 0xc0c0;
        SetVolume();
    }
#endif
    return;
#ifdef DISABLE_USB
 cancelpauseoff:
    cs.cancel = 1;
    player.pauseOn = 0;
#endif
}



void UartInit(void) {
    Disable();
    uartRxWrPtr = uartRxRdPtr = uartRxBuffer;
    WriteIRam(0x20+INTV_RX, ReadIRam((u_int16)MyRxIntCommand));
    //PERIP(INT_ENABLEL) |= INTF_RX;
    Enable();
#if 0
    /*read out any remaining chars*/
    while (UartFill()) {
        UartGetByte();
    }
#endif
}
