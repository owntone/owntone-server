CC=gcc
CFLAGS := $(CFLAGS) -g -I/opt/local/include -DHAVE_CONFIG_H -I. -I..  -DHOST='"foo"' -DHAVE_SQL
LDFLAGS := $(LDFLAGS) -L/opt/local/lib -lid3tag -logg -lvorbisfile -lFLAC -lvorbis -ltag_c -lsqlite -lsqlite3
TARGET = scanner
OBJECTS=scanner-driver.o restart.o err.o scan-wma.o scan-aac.o scan-wav.o scan-flac.o scan-ogg.o scan-mp3.o scan-url.o scan-mpc.o os-unix.o conf.o ll.o xml-rpc.o webserver.o uici.o rend-win32.o configfile.o db-generic.o ssc.o db-sql-sqlite3.o db-sql-sqlite2.o db-sql.o smart-parser.o

$(TARGET):	$(OBJECTS)
	$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS)

clean:
	rm -f $(OBJECTS) $(TARGET)
