CC=gcc
CFLAGS := $(CFLAGS) -g -I/sw/include -DHAVE_CONFIG_H -I. -I..
LDFLAGS := $(LDFLAGS) -L/sw/lib -logg -lvorbisfile -lFLAC -lvorbis

OBJECTS=scanner-driver.o restart.o wma.o err.o flac.o ogg.o

scanner:	$(OBJECTS)
	$(CC) -o scanner $(LDFLAGS) $(OBJECTS)

