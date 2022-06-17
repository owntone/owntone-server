#
# Makefile for adding the web interface source files to the distribution
#
# Building a Vue.js application with Vite / npm does not support VPATH builds.
# Therefor hooking the "npm build" command into the autotools build results
# in "make distcheck" failing.
#
# This Makefile only adds the source files to the distribution tar file.
# To run the web interface build, special make targets exist in this Makefile
# (and in the root Makefile). The important ones are:
#
# - make web-build: builds the web interface (output files are in ../htdocs)
# - make web-clean: deletes files from ../htdocs that were created by the build
# - make web-dist: creates a tar file for the web interface build output
# - make web-serve: runs a local development server
#
# https://www.gnu.org/software/automake/manual/html_node/Third_002dParty-Makefiles.html
#

EXTRA_DIST = public \
	src \
	.eslintrc.js \
	.prettierrc.json \
	index.html \
	package.json \
	package-lock.json \
	vite.config.js

HTDOCS_DIR = ../htdocs

web-serve: node_modules
	npm run serve

web: node_modules
	npm run build

web-update:
	npm update

web-lint: node_modules
	npm run lint

web-format: node_modules
	npm run format

web-clean:
	rm -rf $(HTDOCS_DIR)/assets
	rm -f $(HTDOCS_DIR)/android-chrome-192x192.png \
		$(HTDOCS_DIR)/android-chrome-512x512.png \
		$(HTDOCS_DIR)/apple-touch-icon.png \
		$(HTDOCS_DIR)/browserconfig.xml \
		$(HTDOCS_DIR)/favicon.ico \
		$(HTDOCS_DIR)/favicon-16x16.png \
		$(HTDOCS_DIR)/favicon-32x32.png \
		$(HTDOCS_DIR)/index.html \
		$(HTDOCS_DIR)/logo.svg \
		$(HTDOCS_DIR)/mstile-150x150.png \
		$(HTDOCS_DIR)/safari-pinned-tab.svg \
		$(HTDOCS_DIR)/site.webmanifest

web-clean-all: web-clean
	rm -rf node_modules

node_modules: $(PKG_CONF)
	npm clean-install

.PHONY: web-serve web web-update web-lint web-format web-clean web-clean-all
