CFLAGS+=-g -Wall
LDFLAGS+=-lpthread

test: ipc.o test.o
	gcc $^ $(LDFLAGS) -o $@

.c.o:
	gcc $(CFLAGS) $< -c -o $@

clean:
	rm -f *.o test
