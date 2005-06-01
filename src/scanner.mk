CC=gcc
CFLAGS := $(CFLAGS) -g -I/sw/include -DHAVE_CONFIG_H -I. -I..
LDFLAGS := $(LDFLAGS) -L/sw/lib -logg -lvorbisfile -lFLAC -lvorbis -bind_at_load
TARGET = scanner
OBJECTS=scanner-driver.o restart.o err.o scan-wma.o scan-aac.o scan-wav.o scan-flac.o scan-ogg.o

$(TARGET):	$(OBJECTS)
	$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS)

clean:
	rm -f $(OBJECTS) $(TARGET)
