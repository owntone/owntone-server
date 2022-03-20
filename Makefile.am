if COND_LIBRESPOTC
LIBRESPOTC_SUBDIR=src/inputs/librespot-c
endif

ACLOCAL_AMFLAGS = -I m4

RPM_SPEC_FILE = owntone.spec
CONF_FILE = owntone.conf
SYSTEMD_SERVICE_FILE = owntone.service
SYSTEMD_TSERVICE_FILE = owntone@.service

if COND_INSTALL_SYSTEMD
systemddir = $(SYSTEMD_DIR)
systemd_DATA = $(SYSTEMD_SERVICE_FILE) $(SYSTEMD_TSERVICE_FILE)
endif

BUILT_SOURCES = $(CONF_FILE) $(SYSTEMD_SERVICE_FILE) $(SYSTEMD_TSERVICE_FILE)

SUBDIRS = $(LIBRESPOTC_SUBDIR) sqlext src htdocs

dist_man_MANS = owntone.8

nobase_dist_doc_DATA = \
	UPGRADING \
	README.md \
	docs/playlists.md \
	docs/integrations/spotify.md \
	docs/integrations/lastfm.md \
	docs/index.md \
	docs/outputs/streaming.md \
	docs/outputs/chromecast.md \
	docs/outputs/airplay.md \
	docs/outputs/local-audio.md \
	docs/installation.md \
	docs/clients/web-interface.md \
	docs/clients/remote.md \
	docs/clients/cli.md \
	docs/clients/supported-clients.md \
	docs/clients/mpd.md \
	docs/smart-playlists.md \
	docs/artwork.md \
	docs/library.md \
	docs/getting-started.md \
	docs/advanced/radio-streams.md \
	docs/advanced/multiple-instances.md \
	docs/advanced/outputs-alsa.md \
	docs/advanced/remote-access.md \
	docs/advanced/outputs-pulse.md \
	docs/json-api.md \
	scripts/pairinghelper.sh

EXTRA_DIST = \
	$(CONF_FILE).in \
	$(SYSTEMD_SERVICE_FILE).in \
	$(SYSTEMD_TSERVICE_FILE).in \
	$(RPM_SPEC_FILE)

install-data-hook:
	$(MKDIR_P) "$(DESTDIR)$(localstatedir)/log"
	$(MKDIR_P) "$(DESTDIR)$(localstatedir)/run"
	$(MKDIR_P) "$(DESTDIR)$(localstatedir)/cache/$(PACKAGE)"
if COND_INSTALL_USER
	( $(GETENT) group $(OWNTONE_GROUP) &> /dev/null || $(GROUPADD) --system $(OWNTONE_GROUP) )
	( $(GETENT) passwd $(OWNTONE_USER) &> /dev/null || $(USERADD) --system --no-create-home --gid $(OWNTONE_GROUP) --groups audio --shell /usr/sbin/nologin $(OWNTONE_USER) )
	( ! $(GETENT) group pulse-access &> /dev/null || $(USERMOD) --append --groups pulse-access $(OWNTONE_USER) )
	$(CHOWN) $(OWNTONE_USER).$(OWNTONE_GROUP) "$(DESTDIR)$(localstatedir)/cache/$(PACKAGE)"
endif
if COND_LIBSPOTIFY
	$(MKDIR_P) "$(DESTDIR)$(localstatedir)/cache/$(PACKAGE)/libspotify"
endif
# we do this manually instead of using sysconf_DATA because it overwrites existing config
if COND_INSTALL_CONF_FILE
	$(MKDIR_P) "$(DESTDIR)$(sysconfdir)"
	[ -f $(DESTDIR)$(sysconfdir)/$(CONF_FILE) ] || $(INSTALL_DATA) $(CONF_FILE) $(DESTDIR)$(sysconfdir)/$(CONF_FILE)
endif

uninstall-hook:
	( cd "$(DESTDIR)$(localstatedir)/cache" && rm -rf $(PACKAGE) )
	( cd "$(DESTDIR)$(localstatedir)/log" && rm -f "$(PACKAGE).log*" )
	( cd "$(DESTDIR)$(localstatedir)/run" && rm -f "$(PACKAGE).pid" )
if COND_INSTALL_USER
	( ! $(GETENT) passwd $(OWNTONE_USER) &> /dev/null || $(USERDEL) $(OWNTONE_USER) )
endif
if COND_INSTALL_CONF_FILE
	( cd "$(DESTDIR)$(sysconfdir)" && rm -f $(CONF_FILE) )
endif

CLEANFILES = $(BUILT_SOURCES)

do_subst = $(SED) -e 's|@sbindir[@]|$(sbindir)|g' \
             -e 's|@localstatedir[@]|$(localstatedir)|g' \
             -e 's|@PACKAGE[@]|$(PACKAGE)|g' \
             -e 's|@OWNTONE_USER[@]|$(OWNTONE_USER)|g'

# these files use $prefix, which is determined at build (not configure) time
$(CONF_FILE) $(SYSTEMD_SERVICE_FILE) $(SYSTEMD_TSERVICE_FILE): Makefile
	$(AM_V_at)rm -f $@ $@-t
	$(AM_V_GEN)$(do_subst) "$(srcdir)/$@.in" > $@-t
	$(AM_V_at)mv $@-t $@

$(CONF_FILE): $(srcdir)/$(CONF_FILE).in

$(SYSTEMD_SERVICE_FILE): $(srcdir)/$(SYSTEMD_SERVICE_FILE).in

$(SYSTEMD_TSERVICE_FILE): $(srcdir)/$(SYSTEMD_TSERVICE_FILE).in
