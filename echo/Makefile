# Makefile for echo

CC = gcc
CFLAGS = -O2 -Wall

all: echo

csapp.o: csapp.c csapp.h
	$(CC) $(CFLAGS) -c csapp.c

echo.o: echo.c csapp.h
	$(CC) $(CFLAGS) -c echo.c

echo: echo.o csapp.o
	$(CC) $(CFLAGS) echo.o csapp.o -o echo

clean:
	rm -f *~ *.o echo core *.tar *.zip *.gzip *.bzip *.gz

