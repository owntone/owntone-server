
web_src := \
	$(shell find %reldir%/src/ -type f) \
	$(shell find %reldir%/public/ -type f) \
	%reldir%/vue.config.js \
	%reldir%/postcss.config.js \
	%reldir%/babel.config.js \
	%reldir%/.eslintrc.js \
	%reldir%/.browserslistrc
conf_src := \
	%reldir%/package.json \
	%reldir%/package-lock.json

htdocsdir := $(datadir)/forked-daapd/htdocs

dist_htdocs_DATA := \
	%reldir%/dist/index.html \
	%reldir%/dist/android-chrome-96x96.png \
	%reldir%/dist/apple-touch-icon.png \
	%reldir%/dist/browserconfig.xml \
	%reldir%/dist/favicon.ico \
	%reldir%/dist/favicon-16x16.png \
	%reldir%/dist/favicon-32x32.png \
	%reldir%/dist/mstile-150x150.png \
	%reldir%/dist/safari-pinned-tab.svg \
	%reldir%/dist/site.webmanifest

htdocsplayercssdir := $(datadir)/forked-daapd/htdocs/player/css

dist_htdocsplayercss_DATA := \
	%reldir%/dist/player/css/app.css \
	%reldir%/dist/player/css/app.css.map \
	%reldir%/dist/player/css/chunk-vendors.css \
	%reldir%/dist/player/css/chunk-vendors.css.map

htdocsplayerfontsdir := $(datadir)/forked-daapd/htdocs/player/fonts

dist_htdocsplayerfonts_DATA := \
	%reldir%/dist/player/fonts/materialdesignicons-webfont.ttf \
	%reldir%/dist/player/fonts/materialdesignicons-webfont.woff2 \
	%reldir%/dist/player/fonts/materialdesignicons-webfont.woff \
	%reldir%/dist/player/fonts/materialdesignicons-webfont.eot

htdocsplayerjsdir := $(datadir)/forked-daapd/htdocs/player/js

dist_htdocsplayerjs_DATA := \
	%reldir%/dist/player/js/app.js \
	%reldir%/dist/player/js/app.js.map \
	%reldir%/dist/player/js/chunk-vendors.js \
	%reldir%/dist/player/js/chunk-vendors.js.map \
	%reldir%/dist/player/js/app-legacy.js \
	%reldir%/dist/player/js/app-legacy.js.map \
	%reldir%/dist/player/js/chunk-vendors-legacy.js \
	%reldir%/dist/player/js/chunk-vendors-legacy.js.map

htdocsplayerimgdir := $(datadir)/forked-daapd/htdocs/player/img

dist_htdocsplayerimg_DATA := \
	%reldir%/dist/player/img/materialdesignicons-webfont.svg

install: %reldir%/dist
	$(INSTALL_DATA) -D -t $(htdocsdir) $(dist_htdocs_DATA)
	$(INSTALL_DATA) -D -t $(htdocsplayercssdir) $(dist_htdocsplayercss_DATA)
	$(INSTALL_DATA) -D -t $(htdocsplayerfontsdir) $(dist_htdocsplayerfonts_DATA)
	$(INSTALL_DATA) -D -t $(htdocsplayerjsdir) $(dist_htdocsplayerjs_DATA)
	$(INSTALL_DATA) -D -t $(htdocsplayerimgdir) $(dist_htdocsplayerimg_DATA)

build: %reldir%/dist

clean:
	-rm -rf %reldir%/dist

lint: %reldir%/node_modules
	$(if $(NPM),$(NPM) run lint --prefix %reldir%)

serve: %reldir%/node_modules
	$(if $(NPM),$(NPM) run serve --prefix %reldir%)

# This rule informs `make` that the `node_modules/` directory is out-of-date
# after changes to `package.json` or `package-lock.json`, and instructs `make` 
# on how to install modules to get back up-to-date.
%reldir%/node_modules: $(conf_src)
	$(if $(NPM),$(NPM) install --prefix %reldir%)

%reldir%/dist: %reldir%/node_modules $(web_src)
	$(if $(NPM),$(NPM) run build --prefix %reldir%)

dist-hook:
	cp -p -r \
		%reldir%/dist \
		%reldir%/public \
		%reldir%/src \
		$(conf_src) \
		$(distdir)/web-src

dist-web: %reldir%/dist
	tar -zcvf "$(PACKAGE)-htdocs-$(PACKAGE_VERSION).tar.gz" -C %reldir%/dist ./

all: build

.PHONY: build lint serve install clean
