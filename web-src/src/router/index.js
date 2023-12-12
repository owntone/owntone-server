import * as types from '@/store/mutation_types'
import { createRouter, createWebHashHistory } from 'vue-router'
import store from '@/store'

const TOP_WITH_TABS = 140
const TOP_WITHOUT_TABS = 110

export const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    {
      component: () => import('@/pages/PageAbout.vue'),
      name: 'about',
      path: '/about'
    },
    {
      component: () => import('@/pages/PageAlbum.vue'),
      meta: { show_progress: true },
      name: 'music-album',
      path: '/music/albums/:id'
    },
    {
      component: () => import('@/pages/PageAlbumSpotify.vue'),
      meta: { show_progress: true },
      name: 'music-spotify-album',
      path: '/music/spotify/albums/:id'
    },
    {
      component: () => import('@/pages/PageAlbums.vue'),
      meta: { has_index: true, has_tabs: true, show_progress: true },
      name: 'music-albums',
      path: '/music/albums'
    },
    {
      component: () => import('@/pages/PageArtist.vue'),
      meta: { has_index: true, show_progress: true },
      name: 'music-artist',
      path: '/music/artists/:id'
    },
    {
      component: () => import('@/pages/PageArtistSpotify.vue'),
      meta: { show_progress: true },
      name: 'music-spotify-artist',
      path: '/music/spotify/artists/:id'
    },
    {
      component: () => import('@/pages/PageArtists.vue'),
      meta: { has_index: true, has_tabs: true, show_progress: true },
      name: 'music-artists',
      path: '/music/artists'
    },
    {
      component: () => import('@/pages/PageArtistTracks.vue'),
      meta: { has_index: true, show_progress: true },
      name: 'music-artist-tracks',
      path: '/music/artists/:id/tracks'
    },
    {
      component: () => import('@/pages/PageAudiobooksAlbum.vue'),
      meta: { show_progress: true },
      name: 'audiobooks-album',
      path: '/audiobooks/albums/:id'
    },
    {
      component: () => import('@/pages/PageAudiobooksAlbums.vue'),
      meta: { has_index: true, has_tabs: true, show_progress: true },
      name: 'audiobooks-albums',
      path: '/audiobooks/albums'
    },
    {
      component: () => import('@/pages/PageAudiobooksArtist.vue'),
      meta: { show_progress: true },
      name: 'audiobooks-artist',
      path: '/audiobooks/artists/:id'
    },
    {
      component: () => import('@/pages/PageAudiobooksArtists.vue'),
      meta: { has_index: true, has_tabs: true, show_progress: true },
      name: 'audiobooks-artists',
      path: '/audiobooks/artists'
    },
    {
      component: () => import('@/pages/PageAudiobooksGenres.vue'),
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
      redirect: { name: 'music-history' }
    },
    {
      component: () => import('@/pages/PageMusic.vue'),
      meta: { has_tabs: true, show_progress: true },
      name: 'music-history',
      path: '/music/history'
    },
    {
      component: () => import('@/pages/PageMusicRecentlyAdded.vue'),
      meta: { has_tabs: true, show_progress: true },
      name: 'music-recently-added',
      path: '/music/recently-added'
    },
    {
      component: () => import('@/pages/PageMusicRecentlyPlayed.vue'),
      meta: { has_tabs: true, show_progress: true },
      name: 'music-recently-played',
      path: '/music/recently-played'
    },
    {
      component: () => import('@/pages/PageMusicSpotify.vue'),
      meta: { has_tabs: true, show_progress: true },
      name: 'music-spotify',
      path: '/music/spotify'
    },
    {
      component: () => import('@/pages/PageMusicSpotifyFeaturedPlaylists.vue'),
      meta: { has_tabs: true, show_progress: true },
      name: 'music-spotify-featured-playlists',
      path: '/music/spotify/featured-playlists'
    },
    {
      component: () => import('@/pages/PageMusicSpotifyNewReleases.vue'),
      meta: { has_tabs: true, show_progress: true },
      name: 'music-spotify-new-releases',
      path: '/music/spotify/new-releases'
    },
    {
      component: () => import('@/pages/PageComposerAlbums.vue'),
      meta: { has_index: true, show_progress: true },
      name: 'music-composer-albums',
      path: '/music/composers/:name/albums'
    },
    {
      component: () => import('@/pages/PageComposerTracks.vue'),
      meta: { has_index: true, show_progress: true },
      name: 'music-composer-tracks',
      path: '/music/composers/:name/tracks'
    },
    {
      component: () => import('@/pages/PageComposers.vue'),
      meta: { has_index: true, has_tabs: true, show_progress: true },
      name: 'music-composers',
      path: '/music/composers'
    },
    {
      component: () => import('@/pages/PageFiles.vue'),
      meta: { show_progress: true },
      name: 'files',
      path: '/files'
    },
    {
      component: () => import('@/pages/PageGenreAlbums.vue'),
      meta: { has_index: true, show_progress: true },
      name: 'genre-albums',
      path: '/genres/:name/albums'
    },
    {
      component: () => import('@/pages/PageGenreTracks.vue'),
      meta: { has_index: true, show_progress: true },
      name: 'genre-tracks',
      path: '/genres/:name/tracks'
    },
    {
      component: () => import('@/pages/PageGenres.vue'),
      meta: { has_index: true, has_tabs: true, show_progress: true },
      name: 'music-genres',
      path: '/music/genres'
    },
    {
      component: () => import('@/pages/PageNowPlaying.vue'),
      name: 'now-playing',
      path: '/now-playing'
    },
    {
      name: 'playlists',
      path: '/playlists',
      redirect: { name: 'playlist-folder', params: { id: 0 } }
    },
    {
      component: () => import('@/pages/PagePlaylistFolder.vue'),
      meta: { show_progress: true },
      name: 'playlist-folder',
      path: '/playlists/:id'
    },
    {
      component: () => import('@/pages/PagePlaylistTracks.vue'),
      meta: { show_progress: true },
      name: 'playlist',
      path: '/playlists/:id/tracks'
    },
    {
      component: () => import('@/pages/PagePlaylistTracksSpotify.vue'),
      meta: { show_progress: true },
      name: 'playlist-spotify',
      path: '/playlists/spotify/:id/tracks'
    },
    {
      component: () => import('@/pages/PagePodcast.vue'),
      meta: { show_progress: true },
      name: 'podcast',
      path: '/podcasts/:id'
    },
    {
      component: () => import('@/pages/PagePodcasts.vue'),
      meta: { show_progress: true },
      name: 'podcasts',
      path: '/podcasts'
    },
    {
      component: () => import('@/pages/PageRadioStreams.vue'),
      meta: { show_progress: true },
      name: 'radio',
      path: '/radio'
    },
    {
      component: () => import('@/pages/PageQueue.vue'),
      name: 'queue',
      path: '/'
    },
    {
      component: () => import('@/pages/PageSearchLibrary.vue'),
      name: 'search-library',
      path: '/search/library'
    },
    {
      component: () => import('@/pages/PageSearchSpotify.vue'),
      name: 'search-spotify',
      path: '/search/spotify'
    },
    {
      component: () => import('@/pages/PageSettingsWebinterface.vue'),
      name: 'settings-webinterface',
      path: '/settings/webinterface'
    },
    {
      component: () => import('@/pages/PageSettingsArtwork.vue'),
      name: 'settings-artwork',
      path: '/settings/artwork'
    },
    {
      component: () => import('@/pages/PageSettingsOnlineServices.vue'),
      name: 'settings-online-services',
      path: '/settings/online-services'
    },
    {
      component: () => import('@/pages/PageSettingsRemotesOutputs.vue'),
      name: 'settings-remotes-outputs',
      path: '/settings/remotes-outputs'
    }
  ],
  scrollBehavior(to, from, savedPosition) {
    const wait_ms = 0
    if (savedPosition) {
      // Use the saved scroll position (browser back/forward navigation)
      return new Promise((resolve, reject) => {
        setTimeout(() => {
          resolve(savedPosition)
        }, wait_ms)
      })
    }

    if (to.path === from.path && to.hash) {
      /*
       * Staying on the same page and jumping to an anchor (e. g. index nav)
       * As there is no transition, there is no timeout added
       */
      const top = to.meta.has_tabs ? TOP_WITH_TABS : TOP_WITHOUT_TABS
      return { behavior: 'smooth', el: to.hash, top }
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
      /*
       * Navigate to a page with index nav that should be hidden automatically
       * If a tab navigation exists, an offset to the "top" anchor is added
       */
      return new Promise((resolve, reject) => {
        const top = to.meta.has_tabs ? TOP_WITH_TABS : TOP_WITHOUT_TABS
        setTimeout(() => {
          resolve({ el: '#top', top })
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
