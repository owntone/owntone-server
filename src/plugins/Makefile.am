
rspdir = ${pkglibdir}/plugins
out_daapdir = ${pkglibdir}/plugins

rsp_LTLIBRARIES=rsp.la
rsp_la_LDFLAGS=-module -avoid-version
rsp_la_SOURCES = rsp.c xml-rpc.c

out_daap_LTLIBRARIES=out-daap.la
out_daap_la_LDFLAGS=-module -avoid-version
out_daap_la_SOURCES=out-daap.c out-daap-proto.c

EXTRA_DIST = rsp.h xml-rpc.h out-daap.h \
	out-daap-proto.h

AM_CFLAGS = -I..

