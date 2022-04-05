CFLAGS += -Wall -Wextra -O3 --save-temps -march=westmere
LFLAGS += -lrt

queue: main.o

	$(CC) $^ -o $@ $(LFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

indent:
	indent -bli0 -npsl -cli0 -l160 *.c

clean:
	rm -f *.[oais] queue
