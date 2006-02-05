# $Id$
CC=gcc
CFLAGS := $(CFLAGS) -g -DHAVE_CONFIG_H -I. -I..
LDFLAGS := $(LDFLAGS) 
TARGET = config
OBJECTS=config-driver.o config.o ll.o

$(TARGET):	$(OBJECTS)
	$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS)

clean:
	rm -f $(OBJECTS) $(TARGET)
