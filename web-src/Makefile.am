WEB_BUILD_DIR = htdocs

PACKAGE_JSON = package-lock.json \
	package.json

WEB_DIRS = @WEBSRC_SUBDIRS@

WEB_FILES = @WEBSRC_SOURCE_FILES@

EXTRA_DIST = $(WEB_FILES) htdocs

# Build the Vue.js application
htdocs: $(WEB_FILES)
	@if [ "$(abs_srcdir)" = "$(abs_builddir)" ]; then \
		echo "In-tree build detected"; \
	else \
		echo "Out-of-tree build detected, copy source files to build dir"; \
		for dir in $(WEB_DIRS); do \
			dest_dir="$(abs_builddir)/$$dir"; \
			mkdir -p "$$dest_dir"; \
		done; \
		for file in $(WEB_FILES); do \
			src_path="$(abs_srcdir)/$$file"; \
			dest_path="$(abs_builddir)/$$file"; \
			cp "$$src_path" "$$dest_path"; \
		done; \
	fi
	$(if $(BUILD_WEBINTERFACE),$(NPM) clean-install --no-progress --no-audit)
	$(if $(BUILD_WEBINTERFACE),$(NPM) run build)

all-local: htdocs

install-data-local:
	mkdir -p $(DESTDIR)$(datadir)/owntone/htdocs
	cp -r $(WEB_BUILD_DIR)/* $(DESTDIR)$(datadir)/owntone/htdocs

uninstall-local:
	rm -rf $(DESTDIR)$(datadir)/owntone/htdocs

# Clean up Vue.js build artifacts
clean-local:
	$(if $(BUILD_WEBINTERFACE),rm -rf $(WEB_BUILD_DIR))

distclean-local:
	@if [ "$(abs_srcdir)" != "$(abs_builddir)" ]; then \
		echo "Cleaning up copied files from out-of-tree build..."; \
		for file in $(WEB_FILES); do \
			rm -f "$(abs_builddir)/$$file"; \
		done; \
	fi
	rm -rf node_modules

node_modules: $(PACKAGE_JSON)
	$(if $(BUILD_WEBINTERFACE),$(NPM) clean-install --no-progress --no-audit)

web: node_modules $(WEB_FILES)
	$(if $(BUILD_WEBINTERFACE),$(NPM) run build)

web-serve: node_modules
	$(if $(BUILD_WEBINTERFACE),$(NPM) run serve)

web-update:
	$(if $(BUILD_WEBINTERFACE),$(NPM) update)

web-check:
	@echo "Verifying that all source files in 'src' and 'public' are listed in WEB_FILES ..."
	@unexpected_files=0; \
	for file in $$(find public src -type f); do \
		if ! echo " $(WEB_FILES) " | grep -q " $$file "; then \
			echo "Unknown web interface source file found: $$file"; \
			unexpected_files=1; \
		fi; \
	done; \
	if [ $$unexpected_files -eq 0 ]; then \
		echo "All source files listed in WEB_FILES."; \
	else \
		echo "FAILURE: Unknown web interface source files found, please add them to WEB_FILES."; \
		exit 1; \
	fi

web-lint: node_modules
	$(if $(BUILD_WEBINTERFACE),$(NPM) run lint --no-fix)

web-format: node_modules
	$(if $(BUILD_WEBINTERFACE),$(NPM) run format)

