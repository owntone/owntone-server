SUBDIRS = tests

noinst_LIBRARIES = librespot-c.a

SHANNON_SRC = \
	src/shannon/ShannonFast.c src/shannon/Shannon.h src/shannon/ShannonInternal.h

PROTO_SRC = \
	src/proto/keyexchange.pb-c.c src/proto/keyexchange.pb-c.h \
	src/proto/authentication.pb-c.c src/proto/authentication.pb-c.h \
	src/proto/mercury.pb-c.c src/proto/mercury.pb-c.h \
	src/proto/metadata.pb-c.c src/proto/metadata.pb-c.h

CORE_SRC = \
	src/librespot-c.c src/connection.c src/channel.c src/crypto.c src/commands.c

librespot_c_a_SOURCES =  \
	$(CORE_SRC)      \
	$(SHANNON_SRC)   \
	$(PROTO_SRC)

noinst_HEADERS =	 \
	librespot-c.h src/librespot-c-internal.h src/connection.h \
	src/channel.h src/crypto.h src/commands.h

EXTRA_DIST = README.md LICENSE
