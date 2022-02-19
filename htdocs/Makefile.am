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
htdocsassetsdir = $(datadir)/owntone/htdocs/assets

dist_htdocsassets_DATA = \
	assets/index.css \
	assets/index.js \
	assets/vendor.js \
	assets/materialdesignicons-webfont.svg \
	assets/materialdesignicons-webfont.ttf \
	assets/materialdesignicons-webfont.woff2 \
	assets/materialdesignicons-webfont.woff \
	assets/materialdesignicons-webfont.eot
endif
