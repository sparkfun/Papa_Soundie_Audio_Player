#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vs1000.h>
#include <minifat.h>
#include <mappertiny.h>

#define DEBUG_LEVEL 0
extern struct FsMapper *map;

/*
  Can be used for freeing a range of clusters (sectors)
  Start sector, length in sectors.
 */
void FsFatFree(u_int32 start, u_int32 length){
    register u_int32 fatSector;
    register u_int16 fatLine;
    length = (length + minifatInfo.fatSectorsPerCluster - 1) / minifatInfo.fatSectorsPerCluster;

    start = (start - minifatInfo.dataStart) / minifatInfo.fatSectorsPerCluster;
    if (minifatInfo.IS_FAT_32) {
        	fatSector = minifatInfo.fatStart + ((s_int32)start >> 7);
        	fatLine   = ((u_int16)start << 2);
    } else {
        	fatSector = minifatInfo.fatStart + ((s_int32)start >> 8);
        	fatLine   = ((u_int16)start << 1);
    }
    fatLine &= 511;

#if DEBUG_LEVEL > 0
    puthex(length);
    putstrp("\p=clusters");
    puthex(start>>16);
    puthex(start);
    putstrp("\p=start cluster");
    puthex(fatSector);
    putch(' ');
    puthex(fatLine);
    putstrp("\p=fat sector,line");
#endif
    goto read;
    while (length) {
        	/* no need to read old link, we do not do sanity checks anymore */
        	#if DEBUG_LEVEL > 3
        	puthex(start);
        	puthex(fatLine);
        	puts("");
        	#endif
        	{
        		register u_int32 t = ++start; /* points to next cluster here */
        		--length;
        		minifatBuffer[fatLine/2] = 0; /* Free it */
        		fatLine += 2;
        		if (minifatInfo.IS_FAT_32) {
        			/*fatLine has been added by 2, so use fatLine/2 again! */
        			minifatBuffer[fatLine/2] = 0;
        			fatLine += 2; /*another 2 makes 4 total*/
        		}
        	}
        	fatLine &= 511;
        	if (fatLine == 0 || length == 0) {
        		#if DEBUG_LEVEL > 0
        		puthex(fatSector);
        		puts("=fatSector Wr");
        		#endif
        		map->Write(map, fatSector, 1, minifatBuffer);
        		#if DEBUG_LEVEL > 0
        		FsPrintSector(fatSector);
        		#endif
        	}
        	if (fatLine != 0) {
        		continue;
        	}
        	fatSector++;
        	read:
        	map->Read(map, minifatInfo.currentSector = fatSector, 1, minifatBuffer);
        	#if DEBUG_LEVEL > 0
        	puthex(fatSector);
        	puts("=fatSector Rd");
        	FsPrintSector(fatSector);
        	#endif
        }
}

