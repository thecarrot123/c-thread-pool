GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant

all: clean test.o thread_pool.o
	gcc $(GCC_FLAGS) test.o thread_pool.o

example: clean thread_pool.o example.c
	gcc $(GCC_FLAGS) thread_pool.o example.c -o example

test.o: test.c
	gcc $(GCC_FLAGS) -c test.c -o test.o

thread_pool.o: thread_pool.c thread_pool.h
	gcc $(GCC_FLAGS) -c thread_pool.c -o thread_pool.o

clean:
	rm -f test.o thread_pool.o a.out example