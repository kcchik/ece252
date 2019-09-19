/**
 * @brief  micros and structures for a simple PNG file
 *
 * Copyright 2018-2019 Yiqing Huang
 *
 * This software may be freely redistributed under the terms of MIT License
 */
#pragma once

/******************************************************************************
 * INCLUDE HEADER FILES
 *****************************************************************************/
#include <stdio.h>

/******************************************************************************
 * STRUCTURES and TYPEDEFS
 *****************************************************************************/
typedef unsigned int  U32;

typedef struct chunk {
    U32 length;  /* length of data in the chunk, host byte order */
    U8  type[4]; /* chunk type */
    U8  *p_data; /* pointer to location where the actual data are */
    U32  crc;    /* CRC field  */
} *chunk_p;

/* note that there are 13 Bytes valid data, compiler will padd 3 bytes to make
   the structure 16 Bytes due to alignment. So do not use the size of this
   structure as the actual data size, use 13 Bytes (i.e DATA_IHDR_SIZE macro).
 */
typedef struct data_IHDR {// IHDR chunk data
    U32 width;        /* width in pixels, big endian   */
    U32 height;       /* height in pixels, big endian  */
    U8  bit_depth;    /* num of bits per sample or per palette index.
                         valid values are: 1, 2, 4, 8, 16 */
    U8  color_type;   /* =0: Grayscale; =2: Truecolor; =3 Indexed-color
                         =4: Greyscale with alpha; =6: Truecolor with alpha */
    U8  compression;  /* only method 0 is defined for now */
    U8  filter;       /* only method 0 is defined for now */
    U8  interlace;    /* =0: no interlace; =1: Adam7 interlace */
} *data_IHDR_p;

/* A simple PNG file format, three chunks only*/
typedef struct simple_PNG {
    struct chunk p_IHDR;
    struct chunk p_IDAT;  /* only handles one IDAT chunk */
    struct chunk p_IEND;
} *simple_PNG_p;

/******************************************************************************
 * FUNCTION PROTOTYPES
 *****************************************************************************/
int is_png(FILE *fp) {
    U8 *buf = malloc(8);
    fread(buf, sizeof(buf), 1, fp);
    if (
        buf[0] == 0x89 &&
        buf[1] == 0x50 &&
        buf[2] == 0x4e &&
        buf[3] == 0x47 &&
        buf[4] == 0x0d &&
        buf[5] == 0x0a &&
        buf[6] == 0x1a &&
        buf[7] == 0x0a
    ) {
        free(buf);
        return 1;
    }
    free(buf);
    return 0;
}

int get_chunk(struct chunk *out, FILE *fp, long offset) {
    U8 *buf = malloc(4);
    fseek(fp, offset, SEEK_SET);
    fread(buf, sizeof(buf), 1, fp);
    int len;
    for (int i = 0; i < 4; i++) {
        len = len * 256 + buf[i];
    }
    out->length = len;

    fseek(fp, offset + 4, SEEK_SET);
    fread(buf, sizeof(buf), 1, fp);
    for (int i = 0; i < sizeof(buf); i++) {
        out->type[i] = buf[i];
    }

    U8 *stream = malloc(len);
    fseek(fp, offset + 8, SEEK_SET);
    fread(stream, len, 1, fp);
    out->p_data = stream;

    fseek(fp, offset + len + 8, SEEK_SET);
    fread(buf, sizeof(buf), 1, fp);
    int crc;
    for (int i = 0; i < 4; i++) {
        crc = crc * 256 + buf[i];
    }
    out->crc = crc;

    free(buf);
    return 1;
}

int get_chunks(struct simple_PNG *out, FILE *fp) {
    get_chunk(&out->p_IHDR, fp, 8);
    get_chunk(&out->p_IDAT, fp, out->p_IHDR.length + 20);
    get_chunk(&out->p_IEND, fp, out->p_IHDR.length + out->p_IDAT.length + 32);
    return 1;
}

int get_IHDR(struct data_IHDR *out, struct chunk ihdr, FILE *fp) {
    int w, h;
    for (int i = 0; i < 4; i++) {
        w = w * 256 + ihdr.p_data[i];
    }
    for (int i = 4; i < 8; i++) {
        h = h * 256 + ihdr.p_data[i];
    }
    out->width = w;
    out->height = h;
    out->bit_depth = ihdr.p_data[8];
    out->color_type = ihdr.p_data[9];
    out->compression = ihdr.p_data[10];
    out->filter = ihdr.p_data[11];
    out->interlace = ihdr.p_data[12];
    return 1;
}

int validate_CRC(struct chunk *chunk) {
    U8 *stream = malloc(chunk->length + 4);
    for (int i = 0; i < 4; i++) {
        stream[i] = chunk->type[i];
    }
    for (int i = 0; i < chunk->length; i++) {
        int j = i + 4;
        stream[j] = chunk->p_data[i];
    }
    unsigned long x = crc(stream, chunk->length + 4);
    if (x != chunk->crc) {
        printf("%.4s chunk CRC error: computed %04x, expected %04x\n", chunk->type, x, chunk->crc);
        free(stream);
        return 0;
    }
    free(stream);
    return 1;
}
