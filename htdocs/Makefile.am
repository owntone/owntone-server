if COND_WEBINTERFACE
WEBINTERFACE_SRC = \
	index.html
endif


htdocsdir = $(datadir)/owntone/htdocs

dist_htdocs_DATA = \
	$(WEBINTERFACE_SRC) \
	android-chrome-192x192.png \
	android-chrome-512x512.png \
	apple-touch-icon.png \
	browserconfig.xml \
	favicon.ico \
	favicon-16x16.png \
	favicon-32x32.png \
	mstile-150x150.png \
	safari-pinned-tab.svg \
	site.webmanifest

if COND_WEBINTERFACE
htdocsplayercssdir = $(datadir)/owntone/htdocs/player/css

dist_htdocsplayercss_DATA = \
	player/css/app.css \
	player/css/app.css.map \
	player/css/chunk-vendors.css \
	player/css/chunk-vendors.css.map

htdocsplayerfontsdir = $(datadir)/owntone/htdocs/player/fonts

dist_htdocsplayerfonts_DATA = \
	player/fonts/materialdesignicons-webfont.ttf \
	player/fonts/materialdesignicons-webfont.woff2 \
	player/fonts/materialdesignicons-webfont.woff \
	player/fonts/materialdesignicons-webfont.eot

htdocsplayerjsdir = $(datadir)/owntone/htdocs/player/js

dist_htdocsplayerjs_DATA = \
	player/js/app.js \
	player/js/app.js.map \
	player/js/chunk-vendors.js \
	player/js/chunk-vendors.js.map \
	player/js/app-legacy.js \
	player/js/app-legacy.js.map \
	player/js/chunk-vendors-legacy.js \
	player/js/chunk-vendors-legacy.js.map

htdocsplayerimgdir = $(datadir)/owntone/htdocs/player/img

dist_htdocsplayerimg_DATA = \
	player/img/materialdesignicons-webfont.svg
endif
