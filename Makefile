CC=gcc
CFLAGS=-g -O0 -fno-omit-frame-pointer -fno-inline -Wall -Wextra

build:
	@echo "build target (no sources yet)"

debug:
	@echo "debug target (will attach gdb later)"

clean:
	rm -f *.o
