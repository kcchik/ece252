SRCS   = png_info.c catpng.c crc.c zutil.c
TARGETS= png_info catpng findpng

all: ${TARGETS}

png_info: png_info.o zutil.o crc.o
	gcc -o $@ $^ -lz -g

catpng: catpng.o zutil.o crc.o
	gcc -o $@ $^ -lz -g

findpng: findpng.o zutil.o crc.o
	gcc -o $@ $^ -lz -g

%.o: %.c
	gcc -Wall -g -std=c99 -c $<

%.d: %.c
	gcc -MM -MF $@ $<

-include $(SRCS:.c=.d)

.PHONY: clean
clean:
	rm -f *.d *.o $(TARGETS)
