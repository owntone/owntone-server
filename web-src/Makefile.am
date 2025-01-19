WEB_BUILD_DIR = htdocs

PACKAGE_JSON = package-lock.json \
	package.json

WEB_DIRS = public \
	src \
	src/components \
	src/filter \
	src/i18n \
	src/lib \
	src/pages \
	src/router \
	src/stores \
	src/templates \
	src/webapi

WEB_FILES = \
	public/android-chrome-512x512.png \
	public/mstile-150x150.png \
	public/browserconfig.xml \
	public/site.webmanifest \
	public/favicon.ico \
	public/android-chrome-192x192.png \
	public/safari-pinned-tab.svg \
	public/apple-touch-icon.png \
	public/favicon-16x16.png \
	public/logo.svg \
	public/favicon-32x32.png \
	src/components/ModalDialogAddRss.vue \
	src/components/ModalDialogPlaylistSpotify.vue \
	src/components/ListPlaylistsSpotify.vue \
	src/components/ListArtistsSpotify.vue \
	src/components/ModalDialogComposer.vue \
	src/components/ListArtists.vue \
	src/components/ControlSlider.vue \
	src/components/ModalDialogArtist.vue \
	src/components/ListPlaylists.vue \
	src/components/ModalDialogArtistSpotify.vue \
	src/components/ModalDialogPlaylistSave.vue \
	src/components/ModalDialogGenre.vue \
	src/components/ModalDialogTrackSpotify.vue \
	src/components/ListTracks.vue \
	src/components/ListGenres.vue \
	src/components/ModalDialogAlbumSpotify.vue \
	src/components/TabsAudiobooks.vue \
	src/components/IndexButtonList.vue \
	src/components/TabsSettings.vue \
	src/components/ModalDialogAddUrlStream.vue \
	src/components/ModalDialogDirectory.vue \
	src/components/ListComposers.vue \
	src/components/CoverArtwork.vue \
	src/components/NavbarItemOutput.vue \
	src/components/ModalDialogAlbum.vue \
	src/components/ControlDropdown.vue \
	src/components/ModalDialogPlaylist.vue \
	src/components/ListDirectories.vue \
	src/components/ModalDialog.vue \
	src/components/ListAlbums.vue \
	src/components/ListAlbumsSpotify.vue \
	src/components/ListItemQueueItem.vue \
	src/components/ListTracksSpotify.vue \
	src/components/LyricsPane.vue \
	src/components/ModalDialogQueueItem.vue \
	src/components/ModalDialogRemotePairing.vue \
	src/components/ModalDialogTrack.vue \
	src/components/ModalDialogUpdate.vue \
	src/components/NavbarBottom.vue \
	src/components/NavbarItemLink.vue \
	src/components/NavbarTop.vue \
	src/components/NotificationList.vue \
	src/components/PlayerButtonConsume.vue \
	src/components/PlayerButtonLyrics.vue \
	src/components/PlayerButtonNext.vue \
	src/components/PlayerButtonPlayPause.vue \
	src/components/PlayerButtonPrevious.vue \
	src/components/PlayerButtonRepeat.vue \
	src/components/PlayerButtonSeekBack.vue \
	src/components/PlayerButtonSeekForward.vue \
	src/components/PlayerButtonShuffle.vue \
	src/components/SettingsCheckbox.vue \
	src/components/SettingsIntfield.vue \
	src/components/SettingsTextfield.vue \
	src/components/TabsMusic.vue \
	src/components/TabsSearch.vue \
	src/filter/index.js \
	src/i18n/index.js \
	src/i18n/de.json \
	src/i18n/en.json \
	src/i18n/fr.json \
	src/i18n/zh-CN.json \
	src/i18n/zh-TW.json \
	src/lib/Audio.js \
	src/lib/SVGRenderer.js \
	src/lib/GroupedList.js \
	src/pages/PageMusicRecentlyPlayed.vue \
	src/pages/PageMusicSpotifyFeaturedPlaylists.vue \
	src/pages/PageAudiobooksArtists.vue \
	src/pages/PageAudiobooksGenres.vue \
	src/pages/PageAlbum.vue \
	src/pages/PageComposerAlbums.vue \
	src/pages/PageMusicSpotify.vue \
	src/pages/PagePlaylistTracks.vue \
	src/pages/PagePodcast.vue \
	src/pages/PageMusicSpotifyNewReleases.vue \
	src/pages/PageRadioStreams.vue \
	src/pages/PageAudiobooksAlbums.vue \
	src/pages/PageComposers.vue \
	src/pages/PageAudiobooksArtist.vue \
	src/pages/PageGenres.vue \
	src/pages/PageMusic.vue \
	src/pages/PageGenreAlbums.vue \
	src/pages/PageAbout.vue \
	src/pages/PageAlbumSpotify.vue \
	src/pages/PageAlbums.vue \
	src/pages/PageArtist.vue \
	src/pages/PageArtistSpotify.vue \
	src/pages/PageArtistTracks.vue \
	src/pages/PageArtists.vue \
	src/pages/PageComposerTracks.vue \
	src/pages/PageFiles.vue \
	src/pages/PageGenreTracks.vue \
	src/pages/PageMusicRecentlyAdded.vue \
	src/pages/PagePlaylistFolder.vue \
	src/pages/PagePlaylistTracksSpotify.vue \
	src/pages/PagePodcasts.vue \
	src/pages/PageQueue.vue \
	src/pages/PageSearchLibrary.vue \
	src/pages/PageSearchSpotify.vue \
	src/pages/PageSettingsOnlineServices.vue \
	src/pages/PageSettingsRemotesOutputs.vue \
	src/pages/PageAudiobooksAlbum.vue \
	src/pages/PageNowPlaying.vue \
	src/pages/PageSettingsArtwork.vue \
	src/pages/PageSettingsWebinterface.vue \
	src/router/index.js \
	src/stores/configuration.js \
	src/stores/library.js \
	src/stores/lyrics.js \
	src/stores/notifications.js \
	src/stores/outputs.js \
	src/stores/player.js \
	src/stores/queue.js \
	src/stores/remotes.js \
	src/stores/search.js \
	src/stores/services.js \
	src/stores/settings.js \
	src/stores/ui.js \
	src/templates/ContentWithHeading.vue \
	src/templates/ContentWithHero.vue \
	src/webapi/index.js \
	src/App.vue \
	src/icons.js \
	src/main.js \
	src/mystyles.scss \
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
	$(if $(NPM),$(NPM) clean-install)

# Build the Vue.js application
htdocs: node_modules $(WEB_FILES)
	$(if $(NPM),$(NPM) run build)

web: htdocs

web-serve: node_modules
	$(if $(NPM),$(NPM) run serve)

web-update:
	$(if $(NPM),$(NPM) update)

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
	$(if $(NPM),$(NPM) run lint --no-fix)

web-format: node_modules
	$(if $(NPM),$(NPM) run format)

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
			rm -f "$(abs_builddir)/$$file"; \
		done; \
	fi
	rm -rf node_modules

