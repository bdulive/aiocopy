CC = gcc
CFLAGS = -Wall -O2 -g
LDFLAGS =
PROGS = aiocopy

all: $(PROGS)

%:  %.c 
	$(CC) $(CFLAGS) $@.c -o $@ $(LDFLAGS) $(LDLIBS)

clean:
	rm -f *.o $(PROGS)
