# Makefile, ECE252
# Yiqing Huang, 2018/11/02

CC = gcc
CFLAGS = -Wall -std=c99 -g # "curl-config --cflags" output is empty
LD = gcc
LDFLAGS = -std=c99 -g
LDLIBS = -lcurl -lz # "curl-config --libs" output

SRCS   = main.c crc.c zutil.c util.c
OBJS  = main.o zutil.o crc.o util.c
TARGETS= paster

all: ${TARGETS}

paster: $(OBJS)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

%.d: %.c
	gcc -MM -MF $@ $<

-include $(SRCS:.c=.d)

.PHONY: clean
clean:
	rm -rf *~ *.d *.o $(TARGETS) *.png *.dSYM
