# $Id$
CC=gcc
CFLAGS := $(CFLAGS) -g -DHAVE_CONFIG_H -I. -I.. -DERR_LEAN
LDFLAGS := $(LDFLAGS) 
TARGET = conf
OBJECTS=config-driver.o conf.o ll.o err.o

$(TARGET):	$(OBJECTS)
	$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS)

clean:
	rm -f $(OBJECTS) $(TARGET)
