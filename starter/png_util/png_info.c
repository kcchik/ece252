/**
 * @biref To demonstrate how to use zutil.c and crc.c functions
 *
 * Copyright 2018-2019 Yiqing Huang
 *
 * This software may be freely redistributed under the terms of MIT License
 */

#include <stdio.h>    /* for printf(), perror()...   */
#include <stdlib.h>   /* for malloc()                */
#include <errno.h>    /* for errno                   */
#include "crc.h"      /* for crc()                   */
#include "zutil.h"    /* for mem_def() and mem_inf() */
#include "lab_png.h"  /* simple PNG data structures  */

int main (int argc, char **argv)
{
    FILE *fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        printf("Unable to open file");
        return -1;
    }
    if (!is_png(fp)) {
        printf("%s: Not a PNG file\n", argv[1]);
        fclose(fp);
        return -1;
    }

    struct simple_PNG png;
    struct chunk ihdr;
    struct chunk idat;
    struct chunk iend;
    png.p_IHDR = ihdr;
    png.p_IDAT = idat;
    png.p_IEND = iend;
    get_chunks(&png, fp);

    if (!validate_CRC(&png.p_IHDR)) {
        fclose(fp);
        return -1;
    }

    struct data_IHDR data_ihdr;
    get_IHDR(&data_ihdr, png.p_IHDR, fp);
    printf("%s: %d x %d\n", argv[1], data_ihdr.width, data_ihdr.height);

    if (!validate_CRC(&png.p_IDAT) || !validate_CRC(&png.p_IEND)) {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}
