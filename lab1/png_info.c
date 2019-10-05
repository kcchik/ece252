#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "crc.h"
#include "zutil.h"
#include "lab_png.h"

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

int main (int argc, char **argv)
{
    FILE *fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        printf("Unable to open file");
        return -1;
    }
    if (!is_png(argv[1])) {
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
