EMPTY_AUTOMAKE_TARGETS = install-exec install-dvi install-html install-info install-ps install-pdf check installcheck dvi pdf ps info html tags ctags

.PHONY: all build distdir install install-data uninstall installdirs mostlyclean clean distclean maintainer-clean lint serve $(EMPTY_AUTOMAKE_TARGETS)



web_src := \
	$(shell find src/ -type f) \
	$(shell find public/ -type f) \
	vue.config.js \
	postcss.config.js \
	babel.config.js \
	.eslintrc.js \
	.browserslistrc
conf_src := \
	package.json \
	package-lock.json

htdocsdir := $(datadir)/forked-daapd/htdocs

dist_htdocs_DATA := \
	htdocs/index.html \
	htdocs/android-chrome-96x96.png \
	htdocs/apple-touch-icon.png \
	htdocs/browserconfig.xml \
	htdocs/favicon.ico \
	htdocs/favicon-16x16.png \
	htdocs/favicon-32x32.png \
	htdocs/mstile-150x150.png \
	htdocs/safari-pinned-tab.svg \
	htdocs/site.webmanifest

htdocsplayercssdir := $(htdocsdir)/player/css

dist_htdocsplayercss_DATA := \
	htdocs/player/css/app.css \
	htdocs/player/css/app.css.map \
	htdocs/player/css/chunk-vendors.css \
	htdocs/player/css/chunk-vendors.css.map

htdocsplayerfontsdir := $(htdocsdir)/player/fonts

dist_htdocsplayerfonts_DATA := \
	htdocs/player/fonts/materialdesignicons-webfont.ttf \
	htdocs/player/fonts/materialdesignicons-webfont.woff2 \
	htdocs/player/fonts/materialdesignicons-webfont.woff \
	htdocs/player/fonts/materialdesignicons-webfont.eot

htdocsplayerjsdir := $(htdocsdir)/player/js

dist_htdocsplayerjs_DATA := \
	htdocs/player/js/app.js \
	htdocs/player/js/app.js.map \
	htdocs/player/js/chunk-vendors.js \
	htdocs/player/js/chunk-vendors.js.map \
	htdocs/player/js/app-legacy.js \
	htdocs/player/js/app-legacy.js.map \
	htdocs/player/js/chunk-vendors-legacy.js \
	htdocs/player/js/chunk-vendors-legacy.js.map

htdocsplayerimgdir := $(htdocsdir)/player/img

dist_htdocsplayerimg_DATA := \
	htdocs/player/img/materialdesignicons-webfont.svg


$(EMPTY_AUTOMAKE_TARGETS):

all: build

build: htdocs

distdir:
	cp -p -r \
		htdocs \
		public \
		src \
		$(conf_src) \
		$(distdir)/web-src

install: install-data

install-data: htdocs
	$(INSTALL_DATA) -D -t $(htdocsdir) $(dist_htdocs_DATA)
	$(INSTALL_DATA) -D -t $(htdocsplayercssdir) $(dist_htdocsplayercss_DATA)
	$(INSTALL_DATA) -D -t $(htdocsplayerfontsdir) $(dist_htdocsplayerfonts_DATA)
	$(INSTALL_DATA) -D -t $(htdocsplayerjsdir) $(dist_htdocsplayerjs_DATA)
	$(INSTALL_DATA) -D -t $(htdocsplayerimgdir) $(dist_htdocsplayerimg_DATA)

uninstall:
	-rm -rf $(htdocsdir)

installdirs:
	$(INSTALL_DATA) -d $(htdocsdir)
	$(INSTALL_DATA) -d $(htdocsplayercssdir)
	$(INSTALL_DATA) -d $(htdocsplayerfontsdir)
	$(INSTALL_DATA) -d $(htdocsplayerjsdir)
	$(INSTALL_DATA) -d $(htdocsplayerimgdir)

mostlyclean:
	-rm -rf htdocs

clean: mostlyclean

distclean:

maintainer-clean:

lint: node_modules
	$(if $(NPM),$(NPM) run lint --prefix %reldir%)

serve: node_modules
	$(if $(NPM),$(NPM) run serve --prefix %reldir%)

# This rule informs `make` that the `node_modules/` directory is out-of-date
# after changes to `package.json` or `package-lock.json`, and instructs `make` 
# on how to install modules to get back up-to-date.
node_modules: $(conf_src)
	$(if $(NPM),$(NPM) install --prefix %reldir%)

htdocs: node_modules $(web_src)
	$(if $(NPM),$(NPM) run build --prefix %reldir%)


