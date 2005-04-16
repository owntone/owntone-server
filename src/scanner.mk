CC=gcc
CFLAGS=-g

scanner:	scanner-driver.o restart.o wma.o err.o
	$(CC) -o scanner scanner-driver.o restart.o wma.o err.o

