# $Id$
#
EXTRA_DIST = README mt-daapd.spec mt-daapd.conf.templ mt-daapd.playlist mt-daapd mt-daapd-gentoo mt-daapd-ssc.sh mt-daapd-ssc.pl mt-daapd-ssc.pl mt-daapd-ssc.sh

BUILT_SOURCES = mt-daapd.conf
sysconf_DATA = mt-daapd.conf
bin_SCRIPTS=mt-daapd-ssc.sh

dbdir="/var/cache/mt-daapd"

mt-daapd.conf: mt-daapd.conf.templ ../config.h
	if [ "$(prefix)" = "/usr" ]; then \
		sed -e s,[@]prefix[@],$(prefix),g -e s,[@]dbdir[@],/var/cache/mt-daapd,g $< >$@; \
	else \
		sed -e s,[@]prefix[@],$(prefix),g -e s,[@]dbdir[@],$(prefix)/var/cache/mt-daapd,g  $< >$@; \
	fi











