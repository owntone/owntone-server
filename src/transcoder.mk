CC=gcc
CFLAGS := $(CFLAGS) -g -I/sw/include -DHAVE_CONFIG_H -I. -I..  -DHOST='"foo"' -DHAVE_SQL -DHAVE_CONFIG_H
LDFLAGS := $(LDFLAGS) -L/sw/lib -lid3tag -logg -lvorbisfile -lFLAC -lvorbis -lsqlite -lsqlite3 -lm
TARGET = transcoder
OBJECTS=transcoder-driver.o restart.o err.o os-unix.o conf.o ll.o webserver.o uici.o configfile.o plugin.o xml-rpc.o db-generic.o smart-parser.o db-sql.o db-sql-sqlite2.o db-sql-sqlite3.o dispatch.o rend-win32.o dynamic-art.o scan-aac.o

$(TARGET):	$(OBJECTS)
	$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS)

clean:
	rm -f $(OBJECTS) $(TARGET)
