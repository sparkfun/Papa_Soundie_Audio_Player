#include <stdio.h>
#include <string.h>
#include <vs1000.h>
#include <minifat.h>
#include <mappertiny.h>

extern struct FsMapper *map;
void puthex(u_int16 d);
void putstrp(register __i0 u_int16 *packedStr);
void FsFatFree(u_int32 start, u_int32 length);

#define DEBUG_LEVEL 0

#define FFDE_OK          0
#define FFDE_FAT12       1
#define FFDE_NOTFOUND    2

/*
  Does not support FAT12.
  Only handles root directory.
 */

auto s_int16 FatDeleteDirEntry(const u_int16 *packedName /*8.3 name*/) {
        register __i1 __y struct FRAGMENT *curFragment;
        __y struct FRAGMENT * __y nextFragment;
        register __b u_int32 currentSector;
        register __i3 int i;

        /* Start at the start of root directory. */
        if (minifatInfo.IS_FAT_32) {
        	nextFragment = FatFragmentList(&minifatFragments[0], 2);
        } else if (minifatInfo.FilSysType != 0x3231) {
        	minifatFragments[0].start = minifatInfo.rootStart | LAST_FRAGMENT;
        	minifatFragments[0].size  = ((s_int16)minifatInfo.BPB_RootEntCnt >> 4);
        	nextFragment = &minifatFragments[1];
        } else {
        	return FFDE_FAT12;	/*FAT12 not supported*/
        }

again:
        curFragment = &minifatFragments[0];
        /* fatInfo.parentDir = */
        currentSector = curFragment->start & 0x7fffffffUL;
        i = 0;
        goto getfirst;
        while (1) {
        	register __d1 u_int16 fn = FatGetByte(i+0); /*first char of filename*/
        	if (fn == 0xe5 || fn == 0) {/* 0xe5 / 0x00 deleted or new entry */
        		/* End of directory, so we are done. */
        		if (fn == 0) {
        			return FFDE_NOTFOUND;
        		}
        	} else {
        		if ((FatGetByte(i+11/*Attr*/) & 0xde) == 0) {
#if DEBUG_LEVEL > 2
        			puts(" FILE");
#endif
        			/* Regular file -- check filename */

        			/* Only check 8.0, if these match, it is not valid.
        			This only restricts the selection of filename,
        			it does not create duplicates.
        			We can then later have any suffix we want when
        			we actually create the entry, i.e. .OGG, .MP3 or .WAV .
        			*/
        			if (!memcmp(minifatBuffer+i/2, packedName, 8/2)) {
        				/* Found! */
        				//putstrp("\pfound!\n");
#if 1 //Delete from directory
        				minifatBuffer[i/2] = 0xe520; //mark deleted
        				map->Write(map, currentSector, 1, minifatBuffer);
#endif

#if 0
        				puthex(i);
        				puthex(currentSector);
        				putstrp("\p = at\n");
#endif
        				FatFragmentList(&minifatFragments[0], ((u_int32)FatGetWord(i+20)<<16)+FatGetWord(i+26));
        				curFragment = minifatFragments;
        				//putstrp("\pgot fragment list\n");
        				while (1) {
#if 0
        					puthex(curFragment->start>>16);
        					puthex(curFragment->start);
        					putch(' ');
        					puthex(curFragment->size);
        					putstrp("\p start,size\p\n");
#endif

#if 1 //Free from FAT
        					FsFatFree(curFragment->start & 0x7fffffff, curFragment->size);
#endif
        					if ((s_int32)curFragment->start < 0) {
        						break;
        					}
        					curFragment++;
        				}
        				return FFDE_OK;
        			}
        		}
        	}
        	i = (i + 32) & 511;
        	if (i == 0) {
        		/* if end of directory block, get the next one */
        		currentSector++;
        		if (currentSector >=	(curFragment->start & 0x7fffffffUL) + curFragment->size) {
        			if ((s_int32)curFragment->start < 0) {
        				return FFDE_NOTFOUND;
        			}
        			curFragment++;
        			currentSector = curFragment->start & 0x7fffffffUL;
        		}
        		#if 0 && defined(USE_DEBUG)
        		puthex(currentSector);
        		#endif
        getfirst:
        		map->Read(map, minifatInfo.currentSector = currentSector, 1, minifatBuffer);
        	}
        }
}
        
