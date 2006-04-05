CC=gcc
CFLAGS := $(CFLAGS) -g -I/opt/local/include -DHAVE_CONFIG_H -I. -I.. -Wall -DHAVE_SQL -no-cpp-precomp -DMAC -DHOST='"argh"'
LDFLAGS := $(LDFLAGS) -L/opt/local/lib -lsqlite -lsqlite3
TARGET=parser
OBJECTS=parser-driver.o smart-parser.o err.o db-sql.o db-generic.o ssc.o db-sql-sqlite3.o db-sql-sqlite2.o conf.o ll.o xml-rpc.o webserver.o uici.o os-unix.o restart.o configfile.o rend-win32.o

$(TARGET):	$(OBJECTS)
	$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS)

