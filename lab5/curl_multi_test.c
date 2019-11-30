#define _GNU_SOURCE
#include <search.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <curl/multi.h>
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>

#define MAX_WAIT_MSECS 30*1000 /* Wait max. 30 seconds */
#define SEED_URL "http://ece252-1.uwaterloo.ca/lab4"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */

#define CT_PNG  "image/png"
#define CT_HTML "text/html"
#define CT_PNG_LEN  9
#define CT_HTML_LEN 9

#define max(a, b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a > _b ? _a : _b; })

typedef struct {
  char *buf;       /* memory to hold a copy of received data */
  size_t size;     /* size of valid data in buf in bytes*/
  size_t max_size; /* max capacity of buf in bytes*/
} RECV_BUF;

int running = 1;

char frontier[1024][256];
int frontier_size = 1;
struct hsearch_data visited = {0};
struct hsearch_data png = {0};
int png_size = 0;

int t = 1;
int m = 50;
int l = 0;
char *logfile;

int write_file(const char *path, const char *in, size_t len)
{
    FILE *fp = NULL;

    if (path == NULL) {
        fprintf(stderr, "write_file: file name is null!\n");
        return -1;
    }

    if (in == NULL) {
        fprintf(stderr, "write_file: input data is null!\n");
        return -1;
    }

    fp = fopen(path, "a");
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

int parse_args(int argc, char** argv)
{
    int c;
    char *str = "option requires an argument";

    while ((c = getopt(argc, argv, "t:m:v:")) != -1) {
        switch (c) {
        case 't':
            t = strtoul(optarg, NULL, 10);
            if (t <= 0) {
                fprintf(stderr, "%s: %s > 0 -- 't'\n", argv[0], str);
                return 0;
            }
            break;
        case 'm':
            m = strtoul(optarg, NULL, 10);
            if (m < 0) {
                fprintf(stderr, "%s: %s >= 0 -- 'm'\n", argv[0], str);
                return 0;
            }
            break;
        case 'v':
            l = 1;
            logfile = optarg;
            break;
        default:
            return 0;
        }
    }
    return 1;
}

htmlDocPtr mem_getdoc(char *buf, int size, const char *url)
{
    int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory(buf, size, url, NULL, opts);

    if (doc == NULL) {
        fprintf(stderr, "Document not parsed successfully.\n");
        return NULL;
    }
    return doc;
}

int is_png(char *buf)
{
    if (
        (uint8_t) buf[0] == 0x89 &&
        (uint8_t) buf[1] == 0x50 &&
        (uint8_t) buf[2] == 0x4e &&
        (uint8_t) buf[3] == 0x47 &&
        (uint8_t) buf[4] == 0x0d &&
        (uint8_t) buf[5] == 0x0a &&
        (uint8_t) buf[6] == 0x1a &&
        (uint8_t) buf[7] == 0x0a
    ) {
        return 1;
    }
    return 0;
}

size_t write_cb(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;

    if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);
        char *q = realloc(p->buf, new_size);
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

int recv_buf_init(RECV_BUF *ptr, size_t max_size)
{
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
    return 0;
}

static void init(CURLM *cm, RECV_BUF *ptr)
{
    CURL *eh = curl_easy_init();
    recv_buf_init(ptr, BUF_SIZE);
    curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(eh, CURLOPT_WRITEDATA, (void *)ptr);
    curl_easy_setopt(eh, CURLOPT_HEADER, 0L);
    curl_easy_setopt(eh, CURLOPT_URL, frontier[frontier_size - 1]);
    curl_easy_setopt(eh, CURLOPT_PRIVATE, (void *)ptr);
    curl_easy_setopt(eh, CURLOPT_VERBOSE, 0L);
    curl_multi_add_handle(cm, eh);

    ENTRY e, *ep;
    e.key = malloc(strlen(frontier[frontier_size - 1]) + 1);
    e.data = "1";
    strcpy(e.key, frontier[frontier_size - 1]);
    hsearch_r(e, ENTER, &ep, &visited);

    frontier_size--;
}

xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath) {
    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        return NULL;
    }
    if (xmlXPathNodeSetIsEmpty(result->nodesetval)) {
        xmlXPathFreeObject(result);
        return NULL;
    }
    return result;
}

int find_http(char *buf, int size, int follow_relative_links, const char *base_url)
{
    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar*) "//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;

    if (buf == NULL) {
        return 1;
    }
    doc = mem_getdoc(buf, size, base_url);
    result = getnodeset (doc, xpath);
    if (result) {
        nodeset = result->nodesetval;
        for (i = 0; i < nodeset->nodeNr; i++) {
            href = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            if (follow_relative_links) {
                xmlChar *old = href;
                href = xmlBuildURI(href, (xmlChar *) base_url);
                xmlFree(old);
            }
            if (href != NULL && !strncmp((const char *)href, "http", 4)) {
                ENTRY e, *ep;
                e.key = malloc(strlen((char *) href) + 1);
                strcpy(e.key, (char *) href);
                hsearch_r(e, FIND, &ep, &visited);
                if (ep == NULL) {
                    strcpy(frontier[frontier_size], (char *) href);
                    frontier_size++;
                }
            }
            xmlFree(href);
        }
        xmlXPathFreeObject (result);
    }
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return 0;
}

int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf) {
    char *url = NULL;
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    find_http(p_recv_buf->buf, p_recv_buf->size, 1, url);
    return 0;
}

int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf) {
    char *eurl = NULL;
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    if (is_png(p_recv_buf->buf)) {
        ENTRY e, *ep;
        e.key = malloc(strlen(eurl) + 1);
        strcpy(e.key, eurl);
        hsearch_r(e, FIND, &ep, &png);
        if (ep == NULL) {
            hsearch_r(e, ENTER, &ep, &png);
            sprintf(eurl, "%s\n", eurl);
            write_file("png_urls.txt", eurl, strlen(eurl));

            png_size++;
            if (png_size >= m) {
                return -4;
            }
        }
    }

    return 0;
}

int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf) {
    long response_code;

    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code >= 400) {
        return 1;
    }

    if (l) {
        char *eurl = NULL;
        curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
        sprintf(eurl, "%s\n", eurl);
        write_file(logfile, eurl, strlen(eurl));
    }

    char *ct = NULL;
    curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);
    if (ct == NULL) {
        return 0;
    }

    if (strstr(ct, CT_HTML)) {
        return process_html(curl_handle, p_recv_buf);
    } else if (strstr(ct, CT_PNG)) {
        return process_png(curl_handle, p_recv_buf);
    }

    return 0;
}

int main(int argc, char** argv)
{
    double times[2];
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[0] = (tv.tv_sec) + tv.tv_usec/1000000.;

    if (!parse_args(argc, argv)) {
        return -1;
    }

    if (m == 0) {
        return 0;
    } else if (m > 50) {
        m = 50;
    }

    FILE *fp = fopen("png_urls.txt", "a");
    fclose(fp);

    if (l) {
        FILE *lp = fopen(logfile, "a");
        fclose(lp);
    }

    curl_global_init(CURL_GLOBAL_ALL);

    strcpy(frontier[0], SEED_URL);
    hcreate_r(1024, &visited);
    hcreate_r(1024, &png);

    CURLM *cm=NULL;
    CURL *eh=NULL;
    CURLMsg *msg=NULL;

    while(running) {
        RECV_BUF *recv_buf = malloc(sizeof(RECV_BUF) * t);
        int still_running=0, msgs_left=0;

        cm = curl_multi_init();

        for (int i = 0; i < t; i++) {
            if (!frontier_size) {
                break;
            }
            init(cm, &recv_buf[i]);
        }

        curl_multi_perform(cm, &still_running);

        do {
            int numfds=0;
            int res = curl_multi_wait(cm, NULL, 0, MAX_WAIT_MSECS, &numfds);
            if (res != CURLM_OK) {
                fprintf(stderr, "error: curl_multi_wait() returned %d\n", res);
                return EXIT_FAILURE;
            }
            curl_multi_perform(cm, &still_running);
        } while(still_running);

        int j = 0;
        // int idk = 0;
        while ((msg = curl_multi_info_read(cm, &msgs_left))) {
            eh = msg->easy_handle;

            RECV_BUF *buf;
            curl_easy_getinfo(eh, CURLINFO_PRIVATE, &buf);

            if (process_data(eh, buf) == -4) {
                curl_multi_remove_handle(cm, eh);
                curl_easy_cleanup(eh);
                running = 0;
                break;
            }

            curl_multi_remove_handle(cm, eh);
            curl_easy_cleanup(eh);
            j++;
        }
    }

    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
    double total = times[1] - times[0];
    printf("findpng3 execution time: %.6lf seconds\n", total);

    hdestroy_r(&visited);
    hdestroy_r(&png);

    curl_multi_cleanup(cm);

    return EXIT_SUCCESS;
}
