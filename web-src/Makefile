
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

install: %reldir%/dist
	$(MKDIR_P) $(datadir)/$(PACKAGE)/htdocs
	cp -r %reldir%/dist/* $(datadir)/$(PACKAGE)/htdocs

build: %reldir%/dist

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

.PHONY: build lint serve install
