#include <stdio.h>
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>
#include "util.h"
#include "crc.h"

int init_chunk(struct chunk *chunk) {
    chunk->length = 0;
    chunk->crc = 0;
    return 1;
}

int get_chunk(struct chunk *out, U8 *buf, long offset) {
    int len;
    for (int i = 0; i < 4; i++) {
        len = len * 256 + buf[i + offset];
    }
    out->length = len;

    for (int i = 0; i < 4; i++) {
        out->type[i] = buf[i + offset + 4];
    }

    for (int i = 0; i < len; i++) {
        out->p_data[i] = buf[i + offset + 8];
    }

    U32 crc;
    for (int i = 0; i < 4; i++) {
        crc = crc * 256 + buf[i + offset + len + 8];
    }
    out->crc = crc;

    return 1;
}

int get_chunks(struct simple_PNG *out, U8 *buf) {
    get_chunk(&out->p_IHDR, buf, 8);
    get_chunk(&out->p_IDAT, buf, out->p_IHDR.length + 20);
    return 1;
}

int catpng(struct simple_PNG *pngs) {
    struct chunk chunk;
    int length = 0;
    U8 *width = malloc(4);
    U32 height = 0;

    for (int i = 0; i < 50; i++) {
        length += pngs[i].p_IDAT.length;
        width = pngs[i].p_IHDR.p_data;
        int h = 0;
        for (int i = 4; i < 8; i++) {
            h = h * 256 + pngs[i].p_IHDR.p_data[i];
        }
        height += h;
    }

    U8 all[length * 16];
    U64 all_len = 0;
    for (int i = 0; i < 50; i++) {
        U8 inflated[pngs[i].p_IDAT.length * 512];
        U64 inflated_len = 0;
        mem_inf(inflated, &inflated_len, pngs[i].p_IDAT.p_data, pngs[i].p_IDAT.length);
        memcpy(all + all_len, inflated, inflated_len);
        all_len += inflated_len;
    }
    U8 *deflated = malloc(length);
    U64 deflated_len = 0;
    mem_def(deflated, &deflated_len, all, all_len, Z_DEFAULT_COMPRESSION);

    FILE *fa = fopen("./all.png", "wb");

    U64 *sig = (unsigned long int *) 0x0a1a0a0d474e5089;
    U32 *ihdr_len = (unsigned int *) 0x0d000000;
    U32 *idat_type = (unsigned int *) 0x54414449;
    U32 *iend_len = (unsigned int *) 0x0;
    U64 *iend_data = (unsigned long int *) 0x826042ae444e4549;

    U8 *ihdr_head = malloc(4);
    ihdr_head[0] = 0x49;
    ihdr_head[1] = 0x48;
    ihdr_head[2] = 0x44;
    ihdr_head[3] = 0x52;

    U8 *ihdr_info = malloc(4);
    ihdr_info[0] = 0x08;
    ihdr_info[1] = 0x06;
    ihdr_info[2] = 0x0;
    ihdr_info[3] = 0x0;
    ihdr_info[4] = 0x0;

    U8 *ihdr_height = malloc(4);
    ihdr_height[0] = (height >> 24) & 0xFF;
    ihdr_height[1] = (height >> 16) & 0xFF;
    ihdr_height[2] = (height >> 8) & 0xFF;
    ihdr_height[3] = height & 0xFF;

    U8 *buf = malloc(17);
    memcpy(buf, ihdr_head, 4);
    memcpy(buf + 4, width, 4);
    memcpy(buf + 8, ihdr_height, 4);
    memcpy(buf + 12, ihdr_info, 5);

    U32 c = crc(buf, 17);
    U32 *c_crc = ntohl(c);

    fwrite(&sig, 8, 1, fa);
    fwrite(&ihdr_len, 4, 1, fa);
    fwrite(buf, 17, 1, fa);
    fwrite(&c_crc, 4, 1, fa);

    U8 *idat_type_2 = malloc(4);
    idat_type_2[0] = 0x49;
    idat_type_2[1] = 0x44;
    idat_type_2[2] = 0x41;
    idat_type_2[3] = 0x54;
    U8 *idat_buf = malloc(length + 4);
    memcpy(idat_buf, idat_type_2, 4);
    memcpy(idat_buf + 4, deflated, length);
    U32 *c_def_len = ntohl(deflated_len);
    c = crc(idat_buf, deflated_len + 4);
    c_crc = ntohl(c);

    fwrite(&c_def_len, 4, 1, fa);
    fwrite(&idat_type, 4, 1, fa);
    fwrite(deflated, deflated_len, 1, fa);
    fwrite(&c_crc, 4, 1, fa);

    fwrite(&iend_len, 4, 1, fa);
    fwrite(&iend_data, 8, 1, fa);

    fclose(fa);
    return 1;
}