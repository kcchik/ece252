CC = gcc
CFLAGS = -Wall -std=c99 -g
LD = gcc
LDFLAGS = -std=c99 -g
LDLIBS = -lcurl -pthread -lz

SRCS = main.c shm_queue.c util.c crc.c zutil.c
OBJS = main.o shm_queue.o util.o crc.o zutil.o
TARGETS= paster2

all: ${TARGETS}

paster2: $(OBJS)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

%.d: %.c
	gcc -MM -MF $@ $<

-include $(SRCS:.c=.d)

.PHONY: clean
clean:
	rm -f *~ *.d *.o *~ $(TARGETS) *.png
