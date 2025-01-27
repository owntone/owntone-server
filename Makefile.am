if COND_SPOTIFY
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

if COND_WEBINTERFACE
WEBINTERFACE_SRC_SUBDIR = web-src
endif

SUBDIRS = $(LIBRESPOTC_SUBDIR) sqlext src $(WEBINTERFACE_SRC_SUBDIR)

dist_man_MANS = owntone.8

nobase_dist_doc_DATA = \
	UPGRADING \
	README.md \
	docs/index.md \
	docs/getting-started.md \
	docs/installation.md \
	docs/configuration.md \
	docs/building.md \
	docs/library.md \
	docs/control-clients/mobile.md \
	docs/control-clients/desktop.md \
	docs/control-clients/web.md \
	docs/control-clients/cli-api.md \
	docs/audio-outputs/airplay.md \
	docs/audio-outputs/chromecast.md \
	docs/audio-outputs/local-audio.md \
	docs/audio-outputs/mobile.md \
	docs/audio-outputs/web.md \
	docs/audio-outputs/roku.md \
	docs/audio-outputs/streaming.md \
	docs/media-clients.md \
	docs/artwork.md \
	docs/playlists.md \
	docs/smart-playlists.md \
	docs/integrations/spotify.md \
	docs/integrations/lastfm.md \
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

#
# Convenience make goals for the web interface
#
web:
	$(MAKE) web -C web-src
web-update:
	$(MAKE) web-update -C web-src
web-serve:
	$(MAKE) web-serve -C web-src
web-check:
	$(MAKE) web-check -C web-src
web-lint:
	$(MAKE) web-lint -C web-src
web-format:
	$(MAKE) web-format -C web-src
