import * as types from '@/store/mutation_types'
import { createRouter, createWebHashHistory } from 'vue-router'
import PageAbout from '@/pages/PageAbout.vue'
import PageAlbum from '@/pages/PageAlbum.vue'
import PageAlbumSpotify from '@/pages/PageAlbumSpotify.vue'
import PageAlbums from '@/pages/PageAlbums.vue'
import PageArtist from '@/pages/PageArtist.vue'
import PageArtistSpotify from '@/pages/PageArtistSpotify.vue'
import PageArtistTracks from '@/pages/PageArtistTracks.vue'
import PageArtists from '@/pages/PageArtists.vue'
import PageAudiobooksAlbum from '@/pages/PageAudiobooksAlbum.vue'
import PageAudiobooksAlbums from '@/pages/PageAudiobooksAlbums.vue'
import PageAudiobooksArtist from '@/pages/PageAudiobooksArtist.vue'
import PageAudiobooksArtists from '@/pages/PageAudiobooksArtists.vue'
import PageAudiobooksGenres from '@/pages/PageAudiobooksGenres.vue'
import PageBrowse from '@/pages/PageBrowse.vue'
import PageBrowseRecentlyAdded from '@/pages/PageBrowseRecentlyAdded.vue'
import PageBrowseRecentlyPlayed from '@/pages/PageBrowseRecentlyPlayed.vue'
import PageBrowseSpotify from '@/pages/PageBrowseSpotify.vue'
import PageBrowseSpotifyNewReleases from '@/pages/PageBrowseSpotifyNewReleases.vue'
import PageBrowseSpotifyFeaturedPlaylists from '@/pages/PageBrowseSpotifyFeaturedPlaylists.vue'
import PageComposerAlbums from '@/pages/PageComposerAlbums.vue'
import PageComposerTracks from '@/pages/PageComposerTracks.vue'
import PageComposers from '@/pages/PageComposers.vue'
import PageFiles from '@/pages/PageFiles.vue'
import PageGenreAlbums from '@/pages/PageGenreAlbums.vue'
import PageGenreTracks from '@/pages/PageGenreTracks.vue'
import PageGenres from '@/pages/PageGenres.vue'
import PagePlaylistFolder from '@/pages/PagePlaylistFolder.vue'
import PagePlaylistTracks from '@/pages/PagePlaylistTracks.vue'
import PagePlaylistTracksSpotify from '@/pages/PagePlaylistTracksSpotify.vue'
import PagePodcast from '@/pages/PagePodcast.vue'
import PagePodcasts from '@/pages/PagePodcasts.vue'
import PageNowPlaying from '@/pages/PageNowPlaying.vue'
import PageQueue from '@/pages/PageQueue.vue'
import PageSettingsWebinterface from '@/pages/PageSettingsWebinterface.vue'
import PageSettingsArtwork from '@/pages/PageSettingsArtwork.vue'
import PageSettingsOnlineServices from '@/pages/PageSettingsOnlineServices.vue'
import PageSettingsRemotesOutputs from '@/pages/PageSettingsRemotesOutputs.vue'
import PageRadioStreams from '@/pages/PageRadioStreams.vue'
import PageSearchLibrary from '@/pages/PageSearchLibrary.vue'
import PageSearchSpotify from '@/pages/PageSearchSpotify.vue'
import store from '@/store'

const TOP_WITH_TABS = 140
const TOP_WITHOUT_TABS = 110

export const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    {
      component: PageAbout,
      name: 'about',
      path: '/about'
    },
    {
      component: PageAlbum,
      meta: { show_progress: true },
      name: 'music-album',
      path: '/music/albums/:id'
    },
    {
      component: PageAlbumSpotify,
      meta: { show_progress: true },
      name: 'music-spotify-album',
      path: '/music/spotify/albums/:id'
    },
    {
      component: PageAlbums,
      meta: { has_index: true, has_tabs: true, show_progress: true },
      name: 'music-albums',
      path: '/music/albums'
    },
    {
      component: PageArtist,
      meta: { show_progress: true, has_index: true },
      name: 'music-artist',
      path: '/music/artists/:id'
    },
    {
      component: PageArtistSpotify,
      meta: { show_progress: true },
      name: 'music-spotify-artist',
      path: '/music/spotify/artists/:id'
    },
    {
      component: PageArtists,
      meta: { has_index: true, has_tabs: true, show_progress: true },
      name: 'music-artists',
      path: '/music/artists'
    },
    {
      component: PageArtistTracks,
      meta: { has_index: true, show_progress: true },
      name: 'music-artist-tracks',
      path: '/music/artists/:id/tracks'
    },
    {
      component: PageAudiobooksAlbum,
      meta: { show_progress: true },
      name: 'audiobooks-album',
      path: '/audiobooks/albums/:id'
    },
    {
      component: PageAudiobooksAlbums,
      meta: { has_index: true, has_tabs: true, show_progress: true },
      name: 'audiobooks-albums',
      path: '/audiobooks/albums'
    },
    {
      component: PageAudiobooksArtist,
      meta: { show_progress: true },
      name: 'audiobooks-artist',
      path: '/audiobooks/artists/:id'
    },
    {
      component: PageAudiobooksArtists,
      meta: { has_index: true, has_tabs: true, show_progress: true },
      name: 'audiobooks-artists',
      path: '/audiobooks/artists'
    },
    {
      component: PageAudiobooksGenres,
      meta: { has_index: true, has_tabs: true, show_progress: true },
      name: 'audiobooks-genres',
      path: '/audiobooks/genres'
    },
    {
      name: 'audiobooks',
      path: '/audiobooks',
      redirect: { name: 'audiobooks-artists' }
    },
    {
      name: 'music',
      path: '/music',
      redirect: { name: 'music-browse' }
    },
    {
      component: PageBrowse,
      meta: { has_tabs: true, show_progress: true },
      name: 'music-browse',
      path: '/music/browse'
    },
    {
      component: PageBrowseRecentlyAdded,
      meta: { has_tabs: true, show_progress: true },
      name: 'music-browse-recently-added',
      path: '/music/browse/recently-added'
    },
    {
      component: PageBrowseRecentlyPlayed,
      meta: { has_tabs: true, show_progress: true },
      name: 'music-browse-recently-played',
      path: '/music/browse/recently-played'
    },
    {
      component: PageBrowseSpotify,
      meta: { has_tabs: true, show_progress: true },
      name: 'music-spotify',
      path: '/music/spotify'
    },
    {
      component: PageBrowseSpotifyFeaturedPlaylists,
      meta: { has_tabs: true, show_progress: true },
      name: 'music-spotify-featured-playlists',
      path: '/music/spotify/featured-playlists'
    },
    {
      component: PageBrowseSpotifyNewReleases,
      meta: { has_tabs: true, show_progress: true },
      name: 'music-spotify-new-releases',
      path: '/music/spotify/new-releases'
    },
    {
      component: PageComposerAlbums,
      meta: { show_progress: true, has_index: true },
      name: 'music-composer-albums',
      path: '/music/composers/:name/albums'
    },
    {
      component: PageComposerTracks,
      meta: { has_index: true, show_progress: true },
      name: 'music-composer-tracks',
      path: '/music/composers/:name/tracks'
    },
    {
      component: PageComposers,
      meta: { has_index: true, has_tabs: true, show_progress: true },
      name: 'music-composers',
      path: '/music/composers'
    },
    {
      component: PageFiles,
      meta: { show_progress: true },
      name: 'files',
      path: '/files'
    },
    {
      component: PageGenreAlbums,
      meta: { has_index: true, show_progress: true },
      name: 'genre-albums',
      path: '/genres/:name/albums'
    },
    {
      component: PageGenreTracks,
      meta: { has_index: true, show_progress: true },
      name: 'genre-tracks',
      path: '/genres/:name/tracks'
    },
    {
      component: PageGenres,
      meta: { has_index: true, has_tabs: true, show_progress: true },
      name: 'music-genres',
      path: '/music/genres'
    },
    {
      component: PageNowPlaying,
      name: 'now-playing',
      path: '/now-playing'
    },
    {
      name: 'playlists',
      path: '/playlists',
      redirect: { name: 'playlist-folder', params: { id: 0 } }
    },
    {
      component: PagePlaylistFolder,
      meta: { show_progress: true },
      name: 'playlist-folder',
      path: '/playlists/:id'
    },
    {
      component: PagePlaylistTracks,
      meta: { show_progress: true },
      name: 'playlist',
      path: '/playlists/:id/tracks'
    },
    {
      component: PagePlaylistTracksSpotify,
      meta: { show_progress: true },
      name: 'playlist-spotify',
      path: '/playlists/spotify/:id/tracks'
    },
    {
      component: PagePodcast,
      meta: { show_progress: true },
      name: 'podcast',
      path: '/podcasts/:id'
    },
    {
      component: PagePodcasts,
      meta: { show_progress: true },
      name: 'podcasts',
      path: '/podcasts'
    },
    {
      component: PageRadioStreams,
      meta: { show_progress: true },
      name: 'radio',
      path: '/radio'
    },
    {
      component: PageQueue,
      name: 'queue',
      path: '/'
    },
    {
      component: PageSearchLibrary,
      name: 'search-library',
      path: '/search/library'
    },
    {
      component: PageSearchSpotify,
      name: 'search-spotify',
      path: '/search/spotify'
    },
    {
      component: PageSettingsWebinterface,
      name: 'settings-webinterface',
      path: '/settings/webinterface'
    },
    {
      component: PageSettingsArtwork,
      name: 'settings-artwork',
      path: '/settings/artwork'
    },
    {
      component: PageSettingsOnlineServices,
      name: 'settings-online-services',
      path: '/settings/online-services'
    },
    {
      component: PageSettingsRemotesOutputs,
      name: 'settings-remotes-outputs',
      path: '/settings/remotes-outputs'
    }
  ],
  scrollBehavior(to, from, savedPosition) {
    const wait_ms = 0
    if (savedPosition) {
      // We have saved scroll position (browser back/forward navigation), use this position
      return new Promise((resolve, reject) => {
        setTimeout(() => {
          resolve(savedPosition)
        }, wait_ms)
      })
    }

    if (to.path === from.path && to.hash) {
      // We are staying on the same page and are jumping to an anchor (e. g. index nav)
      // We don't have a transition, so don't add a timeout!
      const top = to.meta.has_tabs ? TOP_WITH_TABS : TOP_WITHOUT_TABS
      return { el: to.hash, top: top, behavior: 'smooth' }
    }

    if (to.hash) {
      // We are navigating to an anchor of a new page, add a timeout to let the transition effect finish before scrolling
      return new Promise((resolve, reject) => {
        setTimeout(() => {
          resolve({ el: to.hash, top: 120 })
        }, wait_ms)
      })
    }

    if (to.meta.has_index) {
      // We are navigating to a page with index nav, that should be hidden automatically
      // Depending on wether we have a tab navigation, add an offset to the "top" anchor
      return new Promise((resolve, reject) => {
        const top = to.meta.has_tabs ? TOP_WITH_TABS : TOP_WITHOUT_TABS
        setTimeout(() => {
          resolve({ el: '#top', top: top })
        }, wait_ms)
      })
    }

    return { left: 0, top: 0 }
  }
})

router.beforeEach((to, from, next) => {
  if (store.state.show_burger_menu) {
    store.commit(types.SHOW_BURGER_MENU, false)
    next(false)
    return
  }
  if (store.state.show_player_menu) {
    store.commit(types.SHOW_PLAYER_MENU, false)
    next(false)
    return
  }
  next(true)
})
