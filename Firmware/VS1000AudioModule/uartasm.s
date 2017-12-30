#include <vs1000.h>
#include "uartctrl.h"

	.sect data_y,uart_bss_y
	.palign RX_BUF_SIZE
	.export _uartRxBuffer
_uartRxBuffer:
	.bss RX_BUF_SIZE

	.sect data_y,init_y
	.export _uartRxWrPtr,_uartRxRdPtr
_uartRxWrPtr:	//must be just before RdPtr
	.word _uartRxBuffer
_uartRxRdPtr:
	.word _uartRxBuffer

	.sect code,my_rx_int
	.export _MyRxIntCommand
_MyRxIntCommand:
	jmpi my_rx_int,(i6)+1
my_rx_int:
	stx i4,(i6)+1	; sty i7,(i6)	//2+12 cycles
	ldc _uartRxWrPtr,i7
	ldy (i7),i4	; stx i5,(i6)

	ldc UART_DATA,i5
	LDP (i5),i3	; sty i3,(i6)
	ldc 0x8000-1+RX_BUF_SIZE,i5
	sty i3,(i4)*

	ldx (i6)-1,i5	; ldy (i6),i3
	ldx (i6),i4	; sty i4,(i7)
	ldc INT_GLOB_EN,i7
	reti
	STP i7,(i7)	; ldy (i6)-1,i7


#if 1
// int UartFill(void) {
//     return ((uartRxWrPtr - uartRxRdPtr) & (SURROUND_RX_BUF_SIZE-1));

	.sect code,UartFill	// 17 words -> 8
	.export _UartFill
_UartFill:	// PROLOGUE
	ldc _uartRxWrPtr,I7
	ldx (i6)+1,null	; ldy (I7)+1,A0
	//ldc _uartRxRdPtr,I7
	stx A1,(I6)	; ldy (I7),A1
	sub A0,A1,A0
	ldc RX_BUF_SIZE-1,A1
	jr
	and A0,A1,A0	; ldx (I6)-1,A1
#endif

#if 1
	.sect code,UartGetByte	// 27 words -> 9 -> 7
	.export _UartGetByte
_UartGetByte:
	ldc _uartRxRdPtr,I7
	ldc 0x8000+RX_BUF_SIZE-1,i5
	ldx (I6)+1,NULL
	ldy (I7),I4	; stx I4,(I6)
	ldy (I4)*,A0
	jr
	sty i4,(i7)	; ldx (I6)-1,i4
#endif

	.end

