CC=gcc
CFLAGS := $(CFLAGS) -g
LDFLAGS := $(LDFLAGS) -logg -lvorbisfile -lFLAC

scanner:	scanner-driver.o restart.o wma.o err.o flac.o ogg.o
	$(CC) -o scanner $(LDFLAGS) scanner-driver.o restart.o wma.o err.o flac.o ogg.o

