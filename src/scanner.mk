CC=gcc
CFLAGS := $(CFLAGS) -g -I/sw/include -DHAVE_CONFIG_H -I. -I..  -DHOST='"foo"' -DHAVE_SQL -DHAVE_CONFIG_H
LDFLAGS := $(LDFLAGS) -L/sw/lib -lid3tag -logg -lvorbisfile -lFLAC -lvorbis -ltag_c -lsqlite -lsqlite3 -lm -framework CoreFoundation
TARGET = scanner
OBJECTS=scanner-driver.o restart.o err.o scan-aif.o scan-wma.o scan-aac.o scan-wav.o scan-flac.o scan-ogg.o scan-mp3.o scan-url.o scan-mpc.o os-unix.o conf.o ll.o xml-rpc.o webserver.o uici.o rend-win32.o configfile.o db-generic.o db-sql-sqlite3.o db-sql-sqlite2.o db-sql.o smart-parser.o plugin.o dispatch.o dynamic-art.o db-sql-updates.o

$(TARGET):	$(OBJECTS)
	$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS)

clean:
	rm -f $(OBJECTS) $(TARGET)
