ACLOCAL_AMFLAGS = -I m4

sysconf_DATA = forked-daapd.conf

EXTRA_DIST = configure
SUBDIRS = src

man_MANS = forked-daapd.8

install-data-hook:
	$(MKDIR_P) $(DESTDIR)$(localstatedir)/cache/forked-daapd

