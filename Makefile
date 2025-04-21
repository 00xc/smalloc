CFLAGS += -Wall -Wextra -Wpedantic -fPIC -std=c99 -O2 -Iinclude/ -mlzcnt -ggdb
LDFLAGS += -shared -fPIE

SHARED_LIB = libsmalloc.so
STATIC_LIB = libsmalloc.a

.PHONY: all clean

all: $(SHARED_LIB) $(STATIC_LIB) example

$(SHARED_LIB): smalloc.o
	$(CC) smalloc.o -o $@ $(LDFLAGS)

$(STATIC_LIB): smalloc.o
	$(AR) rcs $@ smalloc.o

example: $(STATIC_LIB) src/example.c
	$(CC) $(CFLAGS) src/example.c -o $@ -l:$(STATIC_LIB) -L.

smalloc.o: src/smalloc.c
	$(CC) $(CFLAGS) -c src/smalloc.c -o $@

clean:
	rm -f $(SHARED_LIB) $(STATIC_LIB)
	rm -f example
	rm -f *.o
