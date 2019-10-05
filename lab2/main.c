/*
 * The code is derived from cURL example and paster.c base code.
 * The cURL example is at URL:
 * https://curl.haxx.se/libcurl/c/getinmemory.html
 * Copyright (C) 1998 - 2018, Daniel Stenberg, <daniel@haxx.se>, et al..
 *
 * The paster.c code is
 * Copyright 2013 Patrick Lam, <p23lam@uwaterloo.ca>.
 *
 * Modifications to the code are
 * Copyright 2018-2019, Yiqing Huang, <yqhuang@uwaterloo.ca>.
 *
 * This software may be freely redistributed under the terms of the X11 license.
 */

/**
 * @file main_wirte_read_cb.c
 * @brief cURL write call back to save received data in a user defined memory first
 *        and then write the data to a file for verification purpose.
 *        cURL header call back extracts data sequence number from header.
 * @see https://curl.haxx.se/libcurl/c/getinmemory.html
 * @see https://curl.haxx.se/libcurl/using/
 * @see https://ec.haxx.se/callback-write.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include <errno.h>
#include <pthread.h>
#include "crc.h"
#include "zutil.h"
#include "util.h"

#define IMG_URL "http://ece252-1.uwaterloo.ca:2520/image?img="
#define DUM_URL "https://example.com/"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */

#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata);
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata);
int recv_buf_init(RECV_BUF *ptr, size_t max_size);
int recv_buf_cleanup(RECV_BUF *ptr);
int write_file(const char *path, const void *in, size_t len);
void *curl_image(void *arg);

/**
 * @brief  cURL header call back function to extract image sequence number from
 *         http header data. An example header for image part n (assume n = 2) is:
 *         X-Ece252-Fragment: 2
 * @param  char *p_recv: header data delivered by cURL
 * @param  size_t size size of each memb
 * @param  size_t nmemb number of memb
 * @param  void *userdata user defined data structurea
 * @return size of header data received.
 * @details this routine will be invoked multiple times by the libcurl until the full
 * header data are received.  we are only interested in the ECE252_HEADER line
 * received so that we can extract the image sequence number from it. This
 * explains the if block in the code.
 */
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata) {
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;

    if (realsize > strlen(ECE252_HEADER) && strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {
        /* extract img sequence number */
        p->seq = atoi(p_recv + strlen(ECE252_HEADER));
    }
    return realsize;
}

/**
 * @brief write callback function to save a copy of received data in RAM.
 *        The received libcurl data are pointed by p_recv,
 *        which is provided by libcurl and is not user allocated memory.
 *        The user allocated memory is at p_userdata. One needs to
 *        cast it to the proper struct to make good use of it.
 *        This function maybe invoked more than once by one invokation of
 *        curl_easy_perform().
 */
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata) {
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;

    if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);
        U8 *q = realloc(p->buf, new_size);
        if (q == NULL) {
            perror("realloc"); /* out of memory */
            return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }

    memcpy(p->buf + p->size, p_recv, realsize); /*copy data from libcurl*/
    p->size += realsize;
    p->buf[p->size] = 0;

    return realsize;
}

int recv_buf_init(RECV_BUF *ptr, size_t max_size) {
    void *p = NULL;

    if (ptr == NULL) {
        return 1;
    }

    p = malloc(max_size);
    if (p == NULL) {
	    return 2;
    }

    ptr->buf = p;
    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1;
    return 0;
}

int recv_buf_cleanup(RECV_BUF *ptr) {
    if (ptr == NULL) {
	    return 1;
    }

    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    return 0;
}

/**
 * @brief output data in memory to a file
 * @param path const char *, output file path
 * @param in  void *, input data to be written to the file
 * @param len size_t, length of the input data in bytes
 */
int write_file(const char *path, const void *in, size_t len) {
    FILE *fp = NULL;

    if (path == NULL) {
        fprintf(stderr, "write_file: file name is null!\n");
        return -1;
    }

    if (in == NULL) {
        fprintf(stderr, "write_file: input data is null!\n");
        return -1;
    }

    fp = fopen(path, "wb");
    if (fp == NULL) {
        perror("fopen");
        return -2;
    }

    if (fwrite(in, 1, len, fp) != len) {
        fprintf(stderr, "write_file: imcomplete write!\n");
        return -3;
    }
    return fclose(fp);
}

pthread_mutex_t lock;
char url[256];
int received[50] = { 0 };
struct simple_PNG pngs[50];

void *curl_image(void *arg) {
    struct thread_args *p = (struct thread_args *) arg;

    CURL *curl_handle;
    CURLcode res;
    RECV_BUF recv_buf;

    curl_handle = curl_easy_init();
    if (curl_handle == NULL) {
        fprintf(stderr, "curl_easy_init: returned NULL\n");
        return 1;
    }

    int complete = 0;
    while (!complete) {
        recv_buf_init(&recv_buf, BUF_SIZE);

        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&recv_buf);
        curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl);
        curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&recv_buf);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        sleep(5);
        res = curl_easy_perform(curl_handle);

        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
            printf("%lu bytes received in memory %p, seq=%d.\n", recv_buf.size, recv_buf.buf, recv_buf.seq);
            pthread_mutex_lock(&lock);
            if (received[recv_buf.seq] == 0) {
                struct simple_PNG png;
                struct chunk ihdr;
                struct chunk idat;
                init_chunk(&ihdr);
                init_chunk(&idat);
                png.p_IHDR = ihdr;
                png.p_IDAT = idat;
                get_chunks(&png, recv_buf.buf);
                pngs[recv_buf.seq] = png;
            }
            received[recv_buf.seq] = 1;
            complete = 1;
            for (int i = 0; i < 50; i++) {
                if (received[i] == 0) {
                    complete = 0;
                }
            }
            pthread_mutex_unlock(&lock);
        }
    }
    curl_easy_cleanup(curl_handle);
    recv_buf_cleanup(&recv_buf);

    return(0);
}

int main(int argc, char** argv) {
    int c;
    int t = 1;
    int n = 1;
    char *str = "option requires an argument";

    while ((c = getopt (argc, argv, "t:n:")) != -1) {
        switch (c) {
        case 't':
	    t = strtoul(optarg, NULL, 10);
	    if (t <= 0) {
                fprintf(stderr, "%s: %s > 0 -- 't'\n", argv[0], str);
                return -1;
            }
            break;
        case 'n':
            n = strtoul(optarg, NULL, 10);
            if (n <= 0 || n > 3) {
                fprintf(stderr, "%s: %s 1, 2, or 3 -- 'n'\n", argv[0], str);
                return -1;
            }
            break;
        default:
            return -1;
        }
    }

    pthread_mutex_init(&lock, NULL);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    sprintf(url, "%s%d", IMG_URL, n);
    printf("%s: URL is %s\n", argv[0], url);

    pthread_t *threads = malloc(sizeof(pthread_t) * t);

    for (int i = 0; i < t; i++) {
        pthread_create(threads + i, NULL, curl_image, NULL);
    }
    for (int i = 0; i < t; i++) {
        pthread_join(threads[i], 0);
        printf("Thread %lu joined.\n", threads[i]);
    }

    catpng(pngs);

    pthread_mutex_destroy(&lock);
    free(threads);
    curl_global_cleanup();
    return 0;
}
