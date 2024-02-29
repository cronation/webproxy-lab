CC = gcc
CFLAGS = -O2 -Wall -I ..

all: adder adder_2

adder: adder.c
	$(CC) $(CFLAGS) -o adder adder.c

adder_2: adder_2.c
	$(CC) $(CFLAGS) -o adder_2 adder_2.c

clean:
	rm -f adder *~
