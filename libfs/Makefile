CC = gcc

CFLAGS	:= -Wall -Wextra -Werror

objs=$(wildcard *.c)
deps=$(patsubst %.c,%.o,$(objs))

lib := libfs.a

all: $(lib)

$(lib): $(deps)
	ar rcs $(lib) $(deps)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf *.a *.o 
