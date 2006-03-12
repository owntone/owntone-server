CC=gcc
CFLAGS := $(CFLAGS) -g -I/opt/local/include -DHAVE_CONFIG_H -I. -I.. -Wall -DERR_LEAN -DHAVE_SQL
LDFLAGS := $(LDFLAGS) -L/opt/local/lib -lsqlite -lsqlite3
TARGET=parser
OBJECTS=parser-driver.o smart-parser.o err.o db-sql.o db-generic.o ssc.o db-sql-sqlite3.o db-sql-sqlite2.o conf.o ll.o

$(TARGET):	$(OBJECTS)
	$(CC) -o $(TARGET) $(LDFLAGS) $(OBJECTS)

