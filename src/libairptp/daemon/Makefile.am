sbin_PROGRAMS = airptpd

DAEMON_CFLAGS = $(CFLAGS) $(LIBEVENT_CFLAGS)
DAEMON_LIBS = $(LIBS) $(LIBEVENT_LIBS)
DAEMON_LDADD = $(top_builddir)/src/libairptp.a -lpthread $(DAEMON_LIBS)

AM_CPPFLAGS = -I$(top_srcdir)

airptpd_SOURCES = airptpd.c
airptpd_LDADD = $(DAEMON_LDADD)
airptpd_CFLAGS = $(DAEMON_CFLAGS)
