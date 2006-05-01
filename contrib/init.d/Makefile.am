EXTRA_DIST=mt-daapd-fedora.templ mt-daapd-gentoo.templ mtdaapd-bsd.templ

BUILT_SOURCES = mt-daapd-fedora mt-daapd-gentoo mtdaapd-bsd

mt-daapd-fedora: mt-daapd-fedora.templ ../../config.h
	sed -e s,[@]prefix[@],$(prefix),g mt-daapd-fedora.templ >$@; \
	chmod +x $@

mt-daapd-gentoo: mt-daapd-gentoo.templ ../../config.h
	sed -e s,[@]prefix[@],$(prefix),g mt-daapd-gentoo.templ >$@; \
	chmod +x $@

mtdaapd-bsd: mtdaapd-bsd.templ ../../config.h
	sed -e s,[@]prefix[@],$(prefix),g mtdaapd-bsd.templ >$@; \
	chmod +x $@




