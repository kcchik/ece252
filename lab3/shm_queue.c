#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include "shm_queue.h"

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

int sizeof_queue(int size) {
  return sizeof(queue) + sizeof_shm_recv_buf(BUF_SIZE) * size;
}

int init_queue(queue *p, int queue_size) {
  if (p == NULL || queue_size == 0) {
    return 1;
  }

  p->size = queue_size;
  p->pos = -1;
  p->items = malloc(sizeof_shm_recv_buf(BUF_SIZE) * queue_size);

  sem_init(&(p->sem_items), 1, 0);
  sem_init(&(p->sem_spaces), 1, queue_size);
  sem_init(&(p->mutex), 1, 1);

  return 0;
}

void destroy_queue(queue *p) {
  if (p != NULL) {
    free(p);
  }
  sem_destroy(&(p->sem_items));
  sem_destroy(&(p->sem_spaces));
  sem_destroy(&(p->mutex));
}

int is_full(queue *p) {
  if (p == NULL) {
    return 0;
  }
  return p->pos == p->size - 1;
}

int is_empty(queue *p) {
  if (p == NULL) {
    return 0;
  }
  return p->pos == -1;
}

int enqueue(queue *p, recv_buf item) {
  if (p == NULL) {
    return -1;
  }

  if (!is_full(p)) {
    (p->pos)++;
    p->items[p->pos].seq = item.seq;
    p->items[p->pos].size = item.size;
    p->items[p->pos].buf = malloc(item.size * 8);
    for (int i = 0; i < item.size; i++) {
      p->items[p->pos].buf[i] = item.buf[i];
    }

    return 0;
  } else {
    return -1;
  }
}

int dequeue(queue *p, recv_buf *item) {
  *item = p->items[0];
  memcpy(item->buf, p->items[0].buf, p->items[0].size);
  for (int i = 0; i < p->pos; i++) {
    p->items[i] = p->items[i + 1];
    memcpy(p->items[i].buf, p->items[i + 1].buf, p->items[i + 1].size);
  }
  (p->pos)--;
  return 0;
}
