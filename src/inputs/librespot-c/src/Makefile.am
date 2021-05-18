noinst_LIBRARIES = librespot-c.a

AM_CPPFLAGS = -I$(top_builddir)

SHANNON_SRC = \
	shannon/ShannonFast.c shannon/Shannon.h shannon/ShannonInternal.h

PROTO_SRC = \
	proto/keyexchange.pb-c.c proto/keyexchange.pb-c.h \
	proto/authentication.pb-c.c proto/authentication.pb-c.h \
	proto/mercury.pb-c.c proto/mercury.pb-c.h \
	proto/metadata.pb-c.c proto/metadata.pb-c.h

CORE_SRC = \
	librespot-c.c connection.c channel.c crypto.c commands.c

librespot_c_a_SOURCES =  \
	$(CORE_SRC)      \
	$(SHANNON_SRC)   \
	$(PROTO_SRC)

noinst_HEADERS =	 \
	librespot-c-internal.h connection.h channel.h crypto.h commands.h
