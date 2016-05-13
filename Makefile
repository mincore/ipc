CFLAGS+=-g -Wall -m32
LDFLAGS+=-lpthread -m32

test: ipc.o test.o
	gcc $^ $(LDFLAGS) -o $@

.c.o:
	gcc $(CFLAGS) $< -c -o $@

clean:
	rm -f *.o test
