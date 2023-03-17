# Target library
lib := libfs.a
CC = gcc
CFLAGS := -Wall -Wextra -Werror
objs := $(wildcard *.c)
deps := $(patsubst %.c,%.o,$(objs))

all: $(lib)

$(lib): $(objs)
	ar rcs $(lib) $(objs)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.a *.o
