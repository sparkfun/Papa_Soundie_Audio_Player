#include <stdio.h>
#include <vs1000.h>
#include <vstypes.h>

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
