PROJECT=mt-daapd
SOURCES=main.c configfile.c err.c webserver.c restart.c uici.c
OBJECTS=$(SOURCES:.c=.o)
CFLAGS=-g -no-cpp-precomp -DDEBUG

all:	$(PROJECT)
	$(CC) $(OBJECTS) -o $(PROJECT)

$(PROJECT):	$(OBJECTS)

clean:
	rm -f $(PROJECT) *\.o *~ \#*


