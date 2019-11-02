#pragma once

#include "zutil.h"

typedef unsigned int  U32;

struct t_recv_buf;

typedef struct recv_buf2 {
    U8 *buf;
    size_t size;
    size_t max_size;
    int seq;
} RECV_BUF;

typedef struct chunk {
    U32 length;
    U8  type[4];
    U8  p_data[7000];
    U32  crc;
} *chunk_p;

typedef struct simple_PNG {
    struct chunk p_IHDR;
    struct chunk p_IDAT;
} *simple_PNG_p;

int init_chunk(struct chunk *chunk);
int get_chunk(struct chunk *out, U8 *buf, long offset);
int get_chunks(struct simple_PNG *out, U8 *buf);
int catpng(struct simple_PNG *pngs);

size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata);
size_t write_cb_curl(char *p_recv, size_t size, size_t nmemb, void *p_userdata);
int sizeof_shm_recv_buf(size_t nbytes);
int shm_recv_buf_init(struct t_recv_buf *ptr, size_t nbytes);
int write_file(const char *path, const void *in, size_t len);
