/*****************************************************************************/
/*                                                                           */
/*                                 fileio.c                                  */
/*                                                                           */
/*                      File I/O for the ar65 archiver                       */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/* (C) 1998-2003 Ullrich von Bassewitz                                       */
/*               Roemerstrasse 52                                            */
/*               D-70794 Filderstadt                                         */
/* EMail:        uz@cc65.org                                                 */
/*                                                                           */
/*                                                                           */
/* This software is provided 'as-is', without any expressed or implied       */
/* warranty.  In no event will the authors be held liable for any damages    */
/* arising from the use of this software.                                    */
/*                                                                           */
/* Permission is granted to anyone to use this software for any purpose,     */
/* including commercial applications, and to alter it and redistribute it    */
/* freely, subject to the following restrictions:                            */
/*                                                                           */
/* 1. The origin of this software must not be misrepresented; you must not   */
/*    claim that you wrote the original software. If you use this software   */
/*    in a product, an acknowledgment in the product documentation would be  */
/*    appreciated but is not required.                                       */
/* 2. Altered source versions must be plainly marked as such, and must not   */
/*    be misrepresented as being the original software.                      */
/* 3. This notice may not be removed or altered from any source              */
/*    distribution.                                                          */
/*                                                                           */
/*****************************************************************************/



#include <string.h>
#include <err.h>

/* common */
#include "xmalloc.h"

/* ar65 */
#include "error.h"
#include "fileio.h"



/*****************************************************************************/
/*                                   Code                                    */
/*****************************************************************************/





unsigned Read8 (FILE* F)
/* Read an 8 bit value from the file */
{
    int C = getc (F);
    if (C == EOF) {
        errx(1, "Read error (file corrupt?)");
    }
    return C;
}



unsigned Read16 (FILE* F)
/* Read a 16 bit value from the file */
{
    unsigned Lo = Read8 (F);
    unsigned Hi = Read8 (F);
    return (Hi << 8) | Lo;
}



unsigned long Read32 (FILE* F)
/* Read a 32 bit value from the file */
{
    unsigned long Lo = Read16 (F);
    unsigned long Hi = Read16 (F);
    return (Hi << 16) | Lo;
}



unsigned long ReadVar (FILE* F)
/* Read a variable size value from the file */
{
    /* The value was written to the file in 7 bit chunks LSB first. If there
    ** are more bytes, bit 8 is set, otherwise it is clear.
    */
    unsigned char C;
    unsigned long V = 0;
    unsigned Shift = 0;
    do {
        /* Read one byte */
        C = Read8 (F);
        /* Encode it into the target value */
        V |= ((unsigned long)(C & 0x7F)) << Shift;
        /* Next value */
        Shift += 7;
    } while (C & 0x80);

    /* Return the value read */
    return V;
}



char* ReadStr (FILE* F)
/* Read a string from the file (the memory will be malloc'ed) */
{
    /* Read the length */
    unsigned Len = ReadVar (F);

    /* Allocate memory and read the string itself */
    char* S = xmalloc (Len + 1);
    ReadData (F, S, Len);

    /* Terminate the string and return it */
    S [Len] = '\0';
    return S;
}



void* ReadData (FILE* F, void* Data, unsigned Size)
/* Read data from the file */
{
    if (fread (Data, 1, Size, F) != Size) {
        Error ("Read error (file corrupt?)");
    }
    return Data;
}
