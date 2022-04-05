CFLAGS += -Wall -Wextra -O3 --save-temps -march=westmere -D_GNU_SOURCE
LFLAGS += -lrt

all: queue

queue: main.o libq.h
	$(CC) main.o -o $@ $(LFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

indent:
	indent -bli0 -npsl -cli0 -l160 main.c libq.h

clean:
	rm -f *.[oais] queue *.bc *~

