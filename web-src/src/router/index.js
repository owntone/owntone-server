import * as types from '@/store/mutation_types'
import { createRouter, createWebHashHistory } from 'vue-router'
import PageAbout from '@/pages/PageAbout.vue'
import PageAlbum from '@/pages/PageAlbum.vue'
import PageAlbums from '@/pages/PageAlbums.vue'
import PageArtist from '@/pages/PageArtist.vue'
import PageArtistTracks from '@/pages/PageArtistTracks.vue'
import PageArtists from '@/pages/PageArtists.vue'
import PageAudiobooksAlbum from '@/pages/PageAudiobooksAlbum.vue'
import PageAudiobooksAlbums from '@/pages/PageAudiobooksAlbums.vue'
import PageAudiobooksArtist from '@/pages/PageAudiobooksArtist.vue'
import PageAudiobooksArtists from '@/pages/PageAudiobooksArtists.vue'
import PageBrowse from '@/pages/PageBrowse.vue'
import PageBrowseRecentlyAdded from '@/pages/PageBrowseRecentlyAdded.vue'
import PageBrowseRecentlyPlayed from '@/pages/PageBrowseRecentlyPlayed.vue'
import PageFiles from '@/pages/PageFiles.vue'
import PageGenre from '@/pages/PageGenre.vue'
import PageGenreTracks from '@/pages/PageGenreTracks.vue'
import PageGenres from '@/pages/PageGenres.vue'
import PagePlaylist from '@/pages/PagePlaylist.vue'
import PagePlaylistSpotify from '@/pages/PagePlaylistSpotify.vue'
import PagePlaylistTracks from '@/pages/PagePlaylistTracks.vue'
import PagePodcast from '@/pages/PagePodcast.vue'
import PagePodcasts from '@/pages/PagePodcasts.vue'
import PageNowPlaying from '@/pages/PageNowPlaying.vue'
import PageQueue from '@/pages/PageQueue.vue'
import PageSettingsWebinterface from '@/pages/PageSettingsWebinterface.vue'
import PageSettingsArtwork from '@/pages/PageSettingsArtwork.vue'
import PageSettingsOnlineServices from '@/pages/PageSettingsOnlineServices.vue'
import PageSettingsRemotesOutputs from '@/pages/PageSettingsRemotesOutputs.vue'
import PageComposers from '@/pages/PageComposers.vue'
import PageComposer from '@/pages/PageComposer.vue'
import PageComposerTracks from '@/pages/PageComposerTracks.vue'
import PageRadioStreams from '@/pages/PageRadioStreams.vue'
import PageSearchLibrary from '@/pages/PageSearchLibrary.vue'
import PageSearchSpotify from '@/pages/PageSearchSpotify.vue'
import SpotifyPageBrowse from '@/pages/SpotifyPageBrowse.vue'
import SpotifyPageBrowseNewReleases from '@/pages/SpotifyPageBrowseNewReleases.vue'
import SpotifyPageBrowseFeaturedPlaylists from '@/pages/SpotifyPageBrowseFeaturedPlaylists.vue'
import SpotifyPageArtist from '@/pages/SpotifyPageArtist.vue'
import SpotifyPageAlbum from '@/pages/SpotifyPageAlbum.vue'

import store from '@/store'

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
      name: 'audiobooks',
      path: '/audiobooks',
      redirect: '/audiobooks/artists'
    },
    {
      name: 'music',
      path: '/music',
      redirect: '/music/browse'
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
      component: PageFiles,
      meta: { show_progress: true },
      name: 'files',
      path: '/files'
    },
    {
      component: PageGenre,
      meta: { has_index: true, show_progress: true },
      path: '/music/genres/:genre',
      name: 'music-genre'
    },
    {
      path: '/music/genres/:genre/tracks',
      name: 'music-genre-tracks',
      component: PageGenreTracks,
      meta: { show_progress: true, has_index: true }
    },
    {
      component: PageGenres,
      meta: { has_index: true, has_tabs: true, show_progress: true },
      path: '/music/genres',
      name: 'music-genres'
    },
    {
      path: '/music/composers',
      name: 'Composers',
      component: PageComposers,
      meta: { show_progress: true, has_tabs: true, has_index: true }
    },
    {
      path: '/music/composers/:composer/albums',
      name: 'ComposerAlbums',
      component: PageComposer,
      meta: { show_progress: true, has_index: true }
    },
    {
      path: '/music/composers/:composer/tracks',
      name: 'ComposerTracks',
      component: PageComposerTracks,
      meta: { show_progress: true, has_index: true }
    },
    {
      component: PageNowPlaying,
      name: 'now-playing',
      path: '/now-playing'
    },
    {
      name: 'playlists',
      redirect: '/playlists/0'
    },
    {
      component: PagePlaylist,
      meta: { show_progress: true },
      name: 'playlist',
      path: '/playlists/:id'
    },
    {
      component: PagePlaylistSpotify,
      meta: { show_progress: true },
      name: 'playlist-spotify',
      path: '/playlists/spotify/:id'
    },
    {
      component: PagePlaylistTracks,
      meta: { show_progress: true },
      name: 'playlist-tracks',
      path: '/playlists/:id/tracks'
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
      name: 'search',
      path: '/search',
      redirect: '/search/library'
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
    },
    {
      path: '/music/spotify',
      name: 'Spotify',
      component: SpotifyPageBrowse,
      meta: { show_progress: true, has_tabs: true }
    },
    {
      path: '/music/spotify/new-releases',
      name: 'Spotify Browse New Releases',
      component: SpotifyPageBrowseNewReleases,
      meta: { show_progress: true, has_tabs: true }
    },
    {
      path: '/music/spotify/featured-playlists',
      name: 'Spotify Browse Featured Playlists',
      component: SpotifyPageBrowseFeaturedPlaylists,
      meta: { show_progress: true, has_tabs: true }
    },
    {
      path: '/music/spotify/artists/:artist_id',
      name: 'Spotify Artist',
      component: SpotifyPageArtist,
      meta: { show_progress: true }
    },
    {
      path: '/music/spotify/albums/:album_id',
      name: 'Spotify Album',
      component: SpotifyPageAlbum,
      meta: { show_progress: true }
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
      return { el: to.hash, top: 120 }
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
        setTimeout(() => {
          if (to.meta.has_tabs) {
            resolve({ el: '#top', top: 140 })
          } else {
            resolve({ el: '#top', top: 110 })
          }
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
