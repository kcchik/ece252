#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <pthread.h>
#include "shm_queue.h"
#include "crc.h"
#include "zutil.h"
#include "util.h"

#define BUF_SIZE 10240

typedef struct t_recv_buf {
  char *buf;
  size_t size;
  size_t max_size;
  int seq;
} recv_buf;

typedef struct t_queue {
  int size;
  int pos;
  recv_buf *items;
  sem_t sem_items;
  sem_t sem_spaces;
  sem_t mutex;
} queue;

typedef struct t_producer_args {
  int shmid;
  int num;
  int id;
  int image_num;
} producer_args;

typedef struct t_consumer_args {
  int shmid;
  int num;
  int id;
  int delay;
} consumer_args;

int produced = 0;
int consumed = 0;
struct simple_PNG pngs[50];

void* producer(void *arguments) {
  struct t_producer_args *args = (struct t_producer_args *) arguments;

  struct t_queue *pqueue;
  pqueue = shmat(args->shmid, NULL, 0);

  recv_buf *p_recv_buf = malloc(sizeof_shm_recv_buf(BUF_SIZE));
  char url[256];
  CURL *curl_handle;
  CURLcode res;

  while(produced <= 50 - args->num) {
    sem_wait(&(pqueue->sem_spaces));
    sem_wait(&(pqueue->mutex));
    sprintf(url, "http://ece252-2.uwaterloo.ca:2530/image?img=%d&part=%d", args->image_num, produced);

    curl_handle = curl_easy_init();
    shm_recv_buf_init(p_recv_buf, BUF_SIZE);
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *) p_recv_buf);
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl);
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *) p_recv_buf);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    res = curl_easy_perform(curl_handle);

    // if (res != CURLE_OK) {
    //   fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    // } else {
    //   printf("produced %d\n", p_recv_buf->seq);
    // }

    enqueue(pqueue, *p_recv_buf);
    produced++;
    curl_easy_cleanup(curl_handle);
    sem_post(&(pqueue->mutex));
    sem_post(&(pqueue->sem_items));
  }

  free(p_recv_buf);
  shmdt(pqueue);
  pthread_exit(NULL);
}

void* consumer(void *arguments) {
  struct t_consumer_args *args = (struct t_consumer_args *) arguments;

  struct t_queue *pqueue;
  pqueue = shmat(args->shmid, NULL, 0);

  while(consumed <= 50 - args->num) {
    usleep(args->delay * 1000);
    recv_buf p_recv_buf;
    sem_wait(&(pqueue->sem_items));
    sem_wait(&(pqueue->mutex));
    dequeue(pqueue, &p_recv_buf);
    sem_post(&(pqueue->sem_spaces));

    struct simple_PNG png;
    struct chunk ihdr;
    struct chunk idat;
    init_chunk(&ihdr);
    init_chunk(&idat);
    png.p_IHDR = ihdr;
    png.p_IDAT = idat;
    get_chunks(&png, (U8 *) p_recv_buf.buf);
    pngs[consumed] = png;
    // printf("consumed %d\n", consumed);
    consumed++;

    sem_post(&(pqueue->mutex));
  }

  shmdt(pqueue);
  // printf("consumer exit\n");
  pthread_exit(NULL);
}

int main(int argc, char** argv) {
  if (argc != 6) {
    printf("must have 6 arguments\n");
    return -1;
  }

  int buffer_size = atoi(argv[1]);
  int num_producers = atoi(argv[2]);
  int num_consumers = atoi(argv[3]);
  int delay = atoi(argv[4]);
  int image_num = atoi(argv[5]);

  if (buffer_size < 1 || num_producers < 1 || num_consumers < 1 || delay < 0 || (image_num < 1 && image_num > 3)) {
    printf("invalid arguments\n");
    return -1;
  }

  double times[2];
  struct timeval tv;
  if (gettimeofday(&tv, NULL) != 0) {
    perror("gettimeofday");
    abort();
  }
  times[0] = (tv.tv_sec) + tv.tv_usec/1000000.;

  int shm_size = sizeof_queue(buffer_size);

  // printf("shm_size = %d.\n", shm_size);
  int shmid = shmget(IPC_PRIVATE, shm_size, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
  if (shmid == -1) {
    perror("shmget");
    abort();
  }

  struct t_queue *pqueue;
  pqueue = shmat(shmid, NULL, 0);
  init_queue(pqueue, buffer_size);
  shmdt(pqueue);

  curl_global_init(CURL_GLOBAL_DEFAULT);

  pthread_t producer_threads[num_producers];
  struct t_producer_args *p_args = malloc(sizeof(struct t_producer_args*));
  p_args->shmid = shmid;
  p_args->num = num_producers;
  p_args->image_num = image_num;

  pthread_t consumer_threads[num_consumers];
  struct t_consumer_args *c_args = malloc(sizeof(struct t_consumer_args*));
  c_args->shmid = shmid;
  c_args->num = num_consumers;
  c_args->delay = delay;

  for (int i = 0; i < num_producers; i++) {
    p_args->id = i;
    pthread_create(&producer_threads[i], NULL, producer, p_args);
  }
  for (int i = 0; i < num_consumers; i++) {
    c_args->id = i;
    pthread_create(&consumer_threads[i], NULL, consumer, c_args);
  }

  for (int i = 0; i < num_producers; i++) {
    pthread_join(producer_threads[i], NULL);
    // printf("producer %d joined\n", i);
  }
  for (int i = 0; i < num_consumers; i++) {
    pthread_join(consumer_threads[i], NULL);
    // printf("consumer %d joined\n", i);
  }

  // pqueue = shmat(shmid, NULL, 0);
  // destroy_queue(pqueue);
  // shmdt(pqueue);

  catpng(pngs);

  if (gettimeofday(&tv, NULL) != 0) {
    perror("gettimeofday");
    abort();
  }
  times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
  double total = times[1] - times[0];
  printf("paster2 execution time: %.6lf seconds\n", total);

  curl_global_cleanup();
  shmctl(shmid, IPC_RMID, NULL);
  return 0;
}
