SUBDIRS = tests

noinst_LIBRARIES = librespot-c.a

SHANNON_SRC = \
	src/shannon/ShannonFast.c \
	src/shannon/Shannon.h \
	src/shannon/ShannonInternal.h

PROTO_FILES = \
	src/proto/keyexchange.proto \
	src/proto/authentication.proto \
	src/proto/mercury.proto \
	src/proto/metadata.proto \
	src/proto/connectivity.proto \
	src/proto/clienttoken.proto \
	src/proto/login5_user_info.proto \
	src/proto/login5.proto \
	src/proto/login5_identifiers.proto \
	src/proto/login5_credentials.proto \
	src/proto/login5_client_info.proto \
	src/proto/login5_challenges_hashcash.proto \
	src/proto/login5_challenges_code.proto \
	src/proto/google_duration.proto \
	src/proto/storage_resolve.proto \
	src/proto/extended_metadata.proto \
	src/proto/extension_kind.proto \
	src/proto/entity_extension_data.proto \
	src/proto/google_any.proto

PROTO_SRC = \
	$(PROTO_FILES:.proto=.pb-c.c) $(PROTO_FILES:.proto=.pb-c.h)

CORE_SRC = \
	src/librespot-c.c \
	src/connection.c \
	src/channel.c \
	src/crypto.c \
	src/commands.c \
	src/http.c

BUILT_SOURCES = \
	$(PROTO_SRC)

librespot_c_a_SOURCES =  \
	$(CORE_SRC)      \
	$(SHANNON_SRC)   \
	$(PROTO_SRC)

noinst_HEADERS =	 \
	librespot-c.h \
	src/librespot-c-internal.h \
	src/connection.h \
	src/channel.h \
	src/crypto.h \
	src/commands.h \
	src/http.h

EXTRA_DIST = \
	README.md \
	LICENSE \
	$(PROTO_FILES)

%.pb-c.c %.pb-c.h: %.proto
	$(AM_V_GEN)$(PROTOC) --proto_path=src/proto --c_out=src/proto $<
