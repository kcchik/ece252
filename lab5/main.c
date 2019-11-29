#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>
#include <semaphore.h>

#define SEED_URL "http://ece252-1.uwaterloo.ca/lab4"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */

#define CT_PNG  "image/png"
#define CT_HTML "text/html"
#define CT_PNG_LEN  9
#define CT_HTML_LEN 9

#define max(a, b) ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a > _b ? _a : _b; })

typedef struct recv_buf2 {
  char *buf;       /* memory to hold a copy of received data */
  size_t size;     /* size of valid data in buf in bytes*/
  size_t max_size; /* max capacity of buf in bytes*/
  int seq;         /* >=0 sequence number extracted from http header <0 indicates an invalid seq number */
} RECV_BUF;

typedef struct list2 {
  char arr[1024][256];
  int size;
  sem_t spaces;
  sem_t items;
} LIST;

struct hsearch_data visited = {0};
struct hsearch_data png = {0};
LIST frontier;
int png_size = 0;
int m = 50;
int t = 1;
int l = 0;
char *log;
pthread_mutex_t visited_m, png_m, frontier_m;
int running = 1;

int is_png(char *buf);
htmlDocPtr mem_getdoc(char *buf, int size, const char *url);
xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath);
int find_http(char *fname, int size, int follow_relative_links, const char *base_url);
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata);
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata);
int recv_buf_init(RECV_BUF *ptr, size_t max_size);
int recv_buf_cleanup(RECV_BUF *ptr);
void cleanup(CURL *curl, RECV_BUF *ptr);
int write_file(const char *path, const char *in, size_t len);
CURL *easy_handle_init(RECV_BUF *ptr, const char *url);
int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf);

htmlDocPtr mem_getdoc(char *buf, int size, const char *url) {
  int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
  htmlDocPtr doc = htmlReadMemory(buf, size, url, NULL, opts);

  if (doc == NULL) {
    fprintf(stderr, "Document not parsed successfully.\n");
    return NULL;
  }
  return doc;
}

xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath) {
  xmlXPathContextPtr context;
  xmlXPathObjectPtr result;

  context = xmlXPathNewContext(doc);
  if (context == NULL) {
    // printf("Error in xmlXPathNewContext\n");
    return NULL;
  }
  result = xmlXPathEvalExpression(xpath, context);
  xmlXPathFreeContext(context);
  if (result == NULL) {
    // printf("Error in xmlXPathEvalExpression\n");
    return NULL;
  }
  if (xmlXPathNodeSetIsEmpty(result->nodesetval)) {
    xmlXPathFreeObject(result);
    // printf("No result\n");
    return NULL;
  }
  return result;
}

int find_http(char *buf, int size, int follow_relative_links, const char *base_url) {
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
        // pthread_mutex_lock(&visited_m);
        hsearch_r(e, FIND, &ep, &visited);
        // pthread_mutex_unlock(&visited_m);
        // free(e.key);
        if (ep == NULL) {
          pthread_mutex_lock(&frontier_m);
          strcpy(frontier.arr[frontier.size], (char *) href);
          frontier.size++;
          pthread_mutex_unlock(&frontier_m);
          sem_post(&frontier.items);
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
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
  int realsize = size * nmemb;
  RECV_BUF *p = userdata;

// #ifdef DEBUG1_
//   printf("%s", p_recv);
// #endif /* DEBUG1_ */

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
  ptr->seq = -1;              /* valid seq should be positive */
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

void cleanup(CURL *curl, RECV_BUF *ptr) {
  curl_easy_cleanup(curl);
  curl_global_cleanup();
  recv_buf_cleanup(ptr);
}

/**
 * @brief output data in memory to a file
 * @param path const char *, output file path
 * @param in  void *, input data to be written to the file
 * @param len size_t, length of the input data in bytes
 */
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

/**
 * @brief create a curl easy handle and set the options.
 * @param RECV_BUF *ptr points to user data needed by the curl write call back function
 * @param const char *url is the target url to fetch resoruce
 * @return a valid CURL * handle upon sucess; NULL otherwise
 * Note: the caller is responsbile for cleaning the returned curl handle
 */
CURL *easy_handle_init(RECV_BUF *ptr, const char *url)
{
  CURL *curl_handle = NULL;

  if (ptr == NULL || url == NULL) {
    return NULL;
  }

  /* init user defined call back function buffer */
  if (recv_buf_init(ptr, BUF_SIZE) != 0) {
    return NULL;
  }
  /* init a curl session */
  curl_handle = curl_easy_init();

  if (curl_handle == NULL) {
    fprintf(stderr, "curl_easy_init: returned NULL\n");
    return NULL;
  }

  /* specify URL to get */
  curl_easy_setopt(curl_handle, CURLOPT_URL, url);

  /* register write call back function to process received data */
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3);
  /* user defined data structure passed to the call back function */
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)ptr);

  /* register header call back function to process received header data */
  curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl);
  /* user defined data structure passed to the call back function */
  curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)ptr);

  /* some servers requires a user-agent field */
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ece252 lab4 crawler");

  /* follow HTTP 3XX redirects */
  curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
  /* continue to send authentication credentials when following locations */
  curl_easy_setopt(curl_handle, CURLOPT_UNRESTRICTED_AUTH, 1L);
  /* max numbre of redirects to follow sets to 5 */
  curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 5L);
  /* supports all built-in encodings */
  curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "");

  /* Max time in seconds that the connection phase to the server to take */
  //curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 5L);
  /* Max time in seconds that libcurl transfer operation is allowed to take */
  //curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 10L);
  /* Time out for Expect: 100-continue response in milliseconds */
  //curl_easy_setopt(curl_handle, CURLOPT_EXPECT_100_TIMEOUT_MS, 0L);

  /* Enable the cookie engine without reading any initial cookies */
  curl_easy_setopt(curl_handle, CURLOPT_COOKIEFILE, "");
  /* allow whatever auth the proxy speaks */
  curl_easy_setopt(curl_handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
  /* allow whatever auth the server speaks */
  curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);

  return curl_handle;
}

int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf) {
  int follow_relative_link = 1;
  char *url = NULL;
  curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
  find_http(p_recv_buf->buf, p_recv_buf->size, follow_relative_link, url);
  return 0;
}

int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf) {
  char *eurl = NULL;
  curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
  if (is_png(p_recv_buf->buf)) {
    ENTRY e, *ep;
    e.key = malloc(strlen(eurl) + 1);
    strcpy(e.key, eurl);
    // pthread_mutex_lock(&png_m);
    hsearch_r(e, FIND, &ep, &png);
    if (ep == NULL) {
      hsearch_r(e, ENTER, &ep, &png);
      sprintf(eurl, "%s\n", eurl);
      write_file("png_urls.txt", eurl, strlen(eurl));

      png_size++;
      if (png_size >= m) {
        // pthread_mutex_unlock(&png_m);
        return -4;
      }
    }
    // pthread_mutex_unlock(&png_m);
    // free(e.key);
  }

  return 0;
}

int is_png(char *buf) {
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

/**
 * @brief process the download data by curl
 * @param CURL *curl_handle is the curl handler
 * @param RECV_BUF p_recv_buf contains the received data.
 * @return 0 on success; non-zero otherwise
 */
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
    write_file(log, eurl, strlen(eurl));
  }

  char *ct = NULL;
  curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);

  if (strstr(ct, CT_HTML)) {
    return process_html(curl_handle, p_recv_buf);
  } else if (strstr(ct, CT_PNG)) {
    return process_png(curl_handle, p_recv_buf);
  }

  return 0;
}

void *crawler() {
  while (running) {
    CURL *curl_handle;
    CURLcode res;
    RECV_BUF recv_buf;
    // if (frontier.size <= 0) {
    //   pthread_exit(NULL);
    // }
    sem_wait(&frontier.items);
    if (!running) {
      break;
    }
    pthread_mutex_lock(&frontier_m);
    frontier.size--;
    curl_handle = easy_handle_init(&recv_buf, frontier.arr[frontier.size]);
    ENTRY e, *ep;
    e.key = malloc(strlen(frontier.arr[frontier.size]) + 1);
    e.data = "1";
    strcpy(e.key, frontier.arr[frontier.size]);
    pthread_mutex_unlock(&frontier_m);
    // pthread_mutex_lock(&visited_m);
    hsearch_r(e, ENTER, &ep, &visited);
    // pthread_mutex_unlock(&visited_m);
    // free(e.key);

    res = curl_easy_perform(curl_handle);
    if (res == CURLE_OK) {
      if (process_data(curl_handle, &recv_buf) == -4) {

        cleanup(curl_handle, &recv_buf);
        running = 0;
        for (int i = 0; i < t; i++) {
          sem_post(&frontier.items);
        }
        pthread_exit(NULL);
      }
    }

    cleanup(curl_handle, &recv_buf);
  }
  pthread_exit(NULL);
}

int main(int argc, char** argv) {
  int c;
  char *str = "option requires an argument";

  while ((c = getopt(argc, argv, "t:m:v:")) != -1) {
    switch (c) {
    case 't':
      t = strtoul(optarg, NULL, 10);
      if (t <= 0) {
        fprintf(stderr, "%s: %s > 0 -- 't'\n", argv[0], str);
        return -1;
      }
      break;
    case 'm':
      m = strtoul(optarg, NULL, 10);
      if (m < 0) {
        fprintf(stderr, "%s: %s >= 0 -- 'm'\n", argv[0], str);
        return -1;
      }
      break;
    case 'v':
      l = 1;
      log = optarg;
      break;
    default:
      return -1;
    }
  }

  FILE *fp = fopen("png_urls.txt", "a");
  fclose(fp);

  if (l) {
    FILE *lp = fopen(log, "a");
    fclose(lp);
  }

  if (m == 0) {
    return 0;
  } else if (m > 50) {
    m = 50;
  }

  double times[2];
  struct timeval tv;
  if (gettimeofday(&tv, NULL) != 0) {
    perror("gettimeofday");
    abort();
  }
  times[0] = (tv.tv_sec) + tv.tv_usec/1000000.;

  char url[256];
  if (optind >= argc) {
    strcpy(url, SEED_URL);
  } else {
    strcpy(url, argv[optind]);
  }
  strcpy(frontier.arr[0], url);
  frontier.size = 1;
  hcreate_r(1024, &visited);
  hcreate_r(1024, &png);

  curl_global_init(CURL_GLOBAL_DEFAULT);

  pthread_mutex_init(&visited_m, NULL);
  pthread_mutex_init(&png_m, NULL);
  pthread_mutex_init(&frontier_m, NULL);
  sem_init(&frontier.items, 0, 1);
  sem_init(&frontier.spaces, 0, 0);

  pthread_t threads[t];
  for (int i = 0; i < t; i++) {
    pthread_create(&threads[i], NULL, crawler, NULL);
  }

  for (int i = 0; i < t; i++) {
    pthread_join(threads[i], NULL);
  }

  if (gettimeofday(&tv, NULL) != 0) {
    perror("gettimeofday");
    abort();
  }
  times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
  double total = times[1] - times[0];
  printf("findpng2 execution time: %.6lf seconds\n", total);

  hdestroy_r(&visited);
  hdestroy_r(&png);

  sem_destroy(&frontier.items);
  sem_destroy(&frontier.spaces);

  pthread_mutex_destroy(&visited_m);
  pthread_mutex_destroy(&png_m);
  pthread_mutex_destroy(&frontier_m);

  return 0;
}
