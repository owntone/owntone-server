ACLOCAL_AMFLAGS = -I m4

RPM_SPEC_FILE = forked-daapd.spec
CONF_FILE = forked-daapd.conf
SYSTEMD_SERVICE_FILE = forked-daapd.service

sysconf_DATA = $(CONF_FILE)

BUILT_SOURCES = $(CONF_FILE) $(SYSTEMD_SERVICE_FILE)

SUBDIRS = sqlext src htdocs

dist_man_MANS = forked-daapd.8

nobase_dist_doc_DATA = \
	UPGRADING \
	README.md \
	README_JSON_API.md \
	README_PULSE.md \
	README_SMARTPL.md \
	scripts/pairinghelper.sh

EXTRA_DIST = \
	$(CONF_FILE).in \
	$(SYSTEMD_SERVICE_FILE).in \
	$(RPM_SPEC_FILE)

install-data-hook:
	$(MKDIR_P) "$(DESTDIR)$(localstatedir)/cache/$(PACKAGE)/libspotify"
	$(MKDIR_P) "$(DESTDIR)$(localstatedir)/log"
	$(MKDIR_P) "$(DESTDIR)$(localstatedir)/run"

CLEANFILES = $(BUILT_SOURCES)

do_subst = $(SED) -e 's|@sbindir[@]|$(sbindir)|g' \
             -e 's|@localstatedir[@]|$(localstatedir)|g' \
             -e 's|@PACKAGE[@]|$(PACKAGE)|g' \
             -e 's|@DAAPD_USER[@]|$(DAAPD_USER)|g'

# these files use $prefix, which is determined at build (not configure) time
$(CONF_FILE) $(SYSTEMD_SERVICE_FILE): Makefile
	$(AM_V_at)rm -f $@ $@-t
	$(AM_V_GEN)$(do_subst) "$(srcdir)/$@.in" > $@-t
	$(AM_V_at)mv $@-t $@

$(CONF_FILE): $(srcdir)/$(CONF_FILE).in

$(SYSTEMD_SERVICE_FILE): $(srcdir)/$(SYSTEMD_SERVICE_FILE).in
