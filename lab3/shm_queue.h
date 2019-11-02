struct t_queue;
struct t_recv_buf;

int sizeof_queue(int size);
int init_queue(struct t_queue *p, int queue_size);
struct t_queue *create_queue(int size);
void destroy_queue(struct t_queue *p);
int is_full(struct t_queue *p);
int is_empty(struct t_queue *p);
int enqueue(struct t_queue *p, struct t_recv_buf item);
int dequeue(struct t_queue *p, struct t_recv_buf *item);
