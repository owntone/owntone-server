WEB_BUILD_DIR = htdocs

PACKAGE_JSON = package-lock.json \
	package.json

WEB_FILES = public \
	src \
	.prettierrc.json \
	eslint.config.js \
	index.html \
	jsconfig.json \
	$(PACKAGE_JSON) \
	vite.config.js

EXTRA_DIST = $(WEB_FILES) htdocs

node_modules: $(PACKAGE_JSON)
	@if [ "$(abs_srcdir)" = "$(abs_builddir)" ]; then \
		echo "In-tree build detected"; \
	else \
		echo "Out-of-tree build detected, copy source files to build dir"; \
		for file in $(WEB_FILES); do \
			src_path="$(abs_srcdir)/$$file"; \
			dest_path="$(abs_builddir)/$$file"; \
			if [ -d "$$src_path" ]; then \
				mkdir -p "$$dest_path"; \
				cp -r "$$src_path"/* "$$dest_path"; \
				chmod -R u+w "$$dest_path"; \
			elif [ -f "$$src_path" ]; then \
				cp "$$src_path" "$$dest_path"; \
			fi; \
		done; \
	fi
	$(if $(NPM),$(NPM) clean-install)

# Build the Vue.js application
htdocs: node_modules $(WEB_FILES)
	$(if $(NPM),$(NPM) run build)

web: htdocs

all-local: htdocs

install-data-local:
	mkdir -p $(DESTDIR)$(datadir)/owntone/htdocs
	cp -r $(WEB_BUILD_DIR)/* $(DESTDIR)$(datadir)/owntone/htdocs

uninstall-local:
	rm -rf $(DESTDIR)$(datadir)/owntone/htdocs

# Clean up Vue.js build artifacts
clean-local:
	rm -rf $(WEB_BUILD_DIR)

distclean-local:
	@if [ "$(abs_srcdir)" != "$(abs_builddir)" ]; then \
		echo "Cleaning up copied files from out-of-tree build..."; \
		for file in $(WEB_FILES); do \
			rm -rf "$(abs_builddir)/$$file"; \
		done; \
	fi
	rm -rf node_modules

