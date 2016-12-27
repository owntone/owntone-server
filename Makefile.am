ACLOCAL_AMFLAGS = -I m4

sysconf_DATA = forked-daapd.conf

SUBDIRS = sqlext src

dist_man_MANS = forked-daapd.8

nobase_dist_doc_DATA = \
	UPGRADING \
	README.md \
	README_PULSE.md \
	README_SMARTPL.md \
	scripts/antlr35_install.sh \
	scripts/freebsd_install_10.1.sh \
	scripts/freebsd_start_10.1.sh \
	scripts/pairinghelper.sh

EXTRA_DIST = \
	forked-daapd.conf

install-data-hook:
	$(MKDIR_P) "$(DESTDIR)$(localstatedir)/cache/$(PACKAGE)/libspotify"

