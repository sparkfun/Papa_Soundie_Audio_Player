#ifndef _UART_H
#define _UART_H

#define RX_BUF_SIZE 16

#ifdef ASM

#else

#include <vstypes.h>
//#include "vs1053/surround.h"

/* Note: the data in uartRxBuffer[] is bytes, not words. */
extern __y u_int16 uartRxBuffer[RX_BUF_SIZE];
extern volatile __y u_int16 * __y uartRxWrPtr;
extern __y u_int16 * __y uartRxRdPtr;

int UartFill(void);
int UartGetByte(void);
void MyRxIntCommand(void);

/** Player support routines, see uart.c. */
void UartLoadCheck(struct CodecServices *cs, s_int16 n);
void UartIdleHook(void);
void UartInit(void); // Redirect RX interrupt etc.

#endif

#endif/*!_UART_H*/
