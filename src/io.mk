CC=gcc
CFLAGS := $(CFLAGS) -DDEBUG -g -DHAVE_UNISTD_H
LDFLAGS := $(LDFLAGS) 
TARGET = io
OBJECTS= io.o io-driver.o bsd-snprintf.o

$(TARGET):	$(OBJECTS)
	$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS)

clean:
	rm -f $(OBJECTS) $(TARGET)
