CC	= gcc
CFLAGS	= -O2 -Wall -Wextra -Werror -g

PROGRAMS= spin_pthread spin_uthread test_uthread tiny

all: $(PROGRAMS)

spin_pthread: spin.o
	$(CC) spin.o -o spin_pthread

spin_uthread: spin.o libuthread.so
	$(CC) spin.o -L. -luthread -Wl,-rpath='$$ORIGIN' -o spin_uthread

spin.o: spin.c
	$(CC) $(CFLAGS) -c spin.c

test_uthread: test_uthread.o libuthread.so
	$(CC) test_uthread.o -L. -luthread -Wl,-rpath='$$ORIGIN' -o test_uthread

test_uthread.o: test_uthread.c
	$(CC) $(CFLAGS) -c test_uthread.c

tiny: tiny.o rio.o libuthread.so
	$(CC) tiny.o rio.o -L. -luthread -Wl,-rpath='$$ORIGIN' -o tiny

tiny.o: tiny.c rio.h
	$(CC) $(CFLAGS) -c tiny.c

rio.o: rio.c rio.h
	$(CC) $(CFLAGS) -c rio.c

libuthread.so: uthread.o stdlib.o unix.o
	$(CC) -shared uthread.o stdlib.o unix.o -o libuthread.so

uthread.o: uthread.c uthread_internal.h
	$(CC) $(CFLAGS) -fPIC -c uthread.c

stdlib.o: stdlib.c uthread_internal.h
	$(CC) $(CFLAGS) -fPIC -c stdlib.c

unix.o: unix.c uthread_internal.h
	$(CC) $(CFLAGS) -fPIC -c unix.c

format:
	clang-format -i --style=file *.c *.h

clean:
	rm -f *.o libuthread.so $(PROGRAMS)

.PHONY: all format clean
