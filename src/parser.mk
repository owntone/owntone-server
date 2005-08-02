CC=gcc
CFLAGS := $(CFLAGS) -g -I/sw/include -DHAVE_CONFIG_H -I. -I.. -Wall
TARGET=parser
OBJECTS=parser-driver.o smart-parser.o err.o

$(TARGET):	$(OBJECTS)
	$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS)

