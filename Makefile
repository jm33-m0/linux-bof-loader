CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -ldl
BOF_CFLAGS = -fPIC -c

.PHONY: all clean

all: loader example.o

# 1. Compile the Loader
# Requires linking with libdl (-ldl) for dlsym/dlopen
loader: loader_linux_amd64.c
	$(CC) $(CFLAGS) loader_linux_amd64.c -o loader $(LDFLAGS)

# 2. Compile the Payload (BOF)
# Must be Position Independent Code (-fPIC) and only compiled to object (-c)
example.o: example.c
	$(CC) $(BOF_CFLAGS) example.c -o example.o

# Clean up build artifacts
clean:
	rm -f loader example.o
