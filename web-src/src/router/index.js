import * as types from '@/store/mutation_types'
import { createRouter, createWebHashHistory } from 'vue-router'
import PageAbout from '@/pages/PageAbout.vue'
import PageAudiobooksAlbum from '@/pages/PageAudiobooksAlbum.vue'
import PageAudiobooksAlbums from '@/pages/PageAudiobooksAlbums.vue'
import PageFiles from '@/pages/PageFiles.vue'
import PagePlaylist from '@/pages/PagePlaylist.vue'
import PagePlaylistSpotify from '@/pages/PagePlaylistSpotify.vue'
import PagePlaylistTracks from '@/pages/PagePlaylistTracks.vue'
import PagePodcast from '@/pages/PagePodcast.vue'
import PagePodcasts from '@/pages/PagePodcasts.vue'
import PageNowPlaying from '@/pages/PageNowPlaying.vue'
import PageQueue from '@/pages/PageQueue.vue'
import PageBrowse from '@/pages/PageBrowse.vue'
import PageBrowseRecentlyAdded from '@/pages/PageBrowseRecentlyAdded.vue'
import PageBrowseRecentlyPlayed from '@/pages/PageBrowseRecentlyPlayed.vue'
import PageArtists from '@/pages/PageArtists.vue'
import PageArtist from '@/pages/PageArtist.vue'
import PageAlbums from '@/pages/PageAlbums.vue'
import PageAlbum from '@/pages/PageAlbum.vue'
import PageGenres from '@/pages/PageGenres.vue'
import PageGenre from '@/pages/PageGenre.vue'
import PageGenreTracks from '@/pages/PageGenreTracks.vue'
import PageArtistTracks from '@/pages/PageArtistTracks.vue'
import PageComposers from '@/pages/PageComposers.vue'
import PageComposer from '@/pages/PageComposer.vue'
import PageComposerTracks from '@/pages/PageComposerTracks.vue'
import PageAudiobooksArtists from '@/pages/PageAudiobooksArtists.vue'
import PageAudiobooksArtist from '@/pages/PageAudiobooksArtist.vue'
import PageRadioStreams from '@/pages/PageRadioStreams.vue'
import PageSearchLibrary from '@/pages/PageSearchLibrary.vue'
import PageSearchSpotify from '@/pages/PageSearchSpotify.vue'
import SpotifyPageBrowse from '@/pages/SpotifyPageBrowse.vue'
import SpotifyPageBrowseNewReleases from '@/pages/SpotifyPageBrowseNewReleases.vue'
import SpotifyPageBrowseFeaturedPlaylists from '@/pages/SpotifyPageBrowseFeaturedPlaylists.vue'
import SpotifyPageArtist from '@/pages/SpotifyPageArtist.vue'
import SpotifyPageAlbum from '@/pages/SpotifyPageAlbum.vue'
import SettingsPageWebinterface from '@/pages/SettingsPageWebinterface.vue'
import SettingsPageArtwork from '@/pages/SettingsPageArtwork.vue'
import SettingsPageOnlineServices from '@/pages/SettingsPageOnlineServices.vue'
import SettingsPageRemotesOutputs from '@/pages/SettingsPageRemotesOutputs.vue'
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
      component: PageAudiobooksAlbum,
      meta: { show_progress: true },
      name: 'audiobook',
      path: '/audiobook/:id'
    },
    {
      path: '/music',
      redirect: '/music/browse'
    },
    {
      path: '/music/browse',
      name: 'Browse',
      component: PageBrowse,
      meta: { show_progress: true, has_tabs: true }
    },
    {
      path: '/music/browse/recently_added',
      name: 'Browse Recently Added',
      component: PageBrowseRecentlyAdded,
      meta: { show_progress: true, has_tabs: true }
    },
    {
      path: '/music/browse/recently_played',
      name: 'Browse Recently Played',
      component: PageBrowseRecentlyPlayed,
      meta: { show_progress: true, has_tabs: true }
    },
    {
      path: '/music/artists',
      name: 'Artists',
      component: PageArtists,
      meta: { show_progress: true, has_tabs: true, has_index: true }
    },
    {
      path: '/music/artists/:artist_id',
      name: 'Artist',
      component: PageArtist,
      meta: { show_progress: true, has_index: true }
    },
    {
      path: '/music/artists/:artist_id/tracks',
      name: 'Tracks',
      component: PageArtistTracks,
      meta: { show_progress: true, has_index: true }
    },
    {
      path: '/music/albums',
      name: 'Albums',
      component: PageAlbums,
      meta: { show_progress: true, has_tabs: true, has_index: true }
    },
    {
      path: '/music/albums/:album_id',
      name: 'Album',
      component: PageAlbum,
      meta: { show_progress: true }
    },
    {
      component: PageFiles,
      meta: { show_progress: true },
      name: 'files',
      path: '/files'
    },
    {
      path: '/music/genres',
      name: 'Genres',
      component: PageGenres,
      meta: { show_progress: true, has_tabs: true, has_index: true }
    },
    {
      path: '/music/genres/:genre',
      name: 'Genre',
      component: PageGenre,
      meta: { show_progress: true, has_index: true }
    },
    {
      path: '/music/genres/:genre/tracks',
      name: 'GenreTracks',
      component: PageGenreTracks,
      meta: { show_progress: true, has_index: true }
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
      redirect: '/playlist/0'
    },
    {
      component: PagePlaylist,
      meta: { show_progress: true },
      name: 'playlist',
      path: '/playlist/:id'
    },
    {
      component: PagePlaylistSpotify,
      meta: { show_progress: true },
      name: 'playlist-spotify',
      path: '/playlist/spotify/:id'
    },
    {
      component: PagePlaylistTracks,
      meta: { show_progress: true },
      name: 'playlist-tracks',
      path: '/playlist/:id/tracks'
    },
    {
      component: PagePodcast,
      meta: { show_progress: true },
      name: 'podcast',
      path: '/podcast/:id'
    },
    {
      component: PagePodcasts,
      meta: { show_progress: true },
      name: 'podcasts',
      path: '/podcasts'
    },
    {
      path: '/audiobooks',
      redirect: '/audiobooks/artists'
    },
    {
      path: '/audiobooks/artists',
      name: 'AudiobooksArtists',
      component: PageAudiobooksArtists,
      meta: { show_progress: true, has_tabs: true, has_index: true }
    },
    {
      path: '/audiobooks/artists/:artist_id',
      name: 'AudiobooksArtist',
      component: PageAudiobooksArtist,
      meta: { show_progress: true }
    },
    {
      path: '/audiobooks/albums',
      name: 'AudiobooksAlbums',
      component: PageAudiobooksAlbums,
      meta: { show_progress: true, has_tabs: true, has_index: true }
    },
    {
      path: '/radio',
      name: 'Radio',
      component: PageRadioStreams,
      meta: { show_progress: true }
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
    },
    {
      path: '/settings/webinterface',
      name: 'Settings Webinterface',
      component: SettingsPageWebinterface
    },
    {
      path: '/settings/artwork',
      name: 'Settings Artwork',
      component: SettingsPageArtwork
    },
    {
      path: '/settings/online-services',
      name: 'Settings Online Services',
      component: SettingsPageOnlineServices
    },
    {
      path: '/settings/remotes-outputs',
      name: 'Settings Remotes Outputs',
      component: SettingsPageRemotesOutputs
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
      // We are staying on the same page and are jumping to an anker (e. g. index nav)
      // We don't have a transition, so don't add a timeout!
      return { el: to.hash, top: 120 }
    }

    if (to.hash) {
      // We are navigating to an anker of a new page, add a timeout to let the transition effect finish before scrolling
      return new Promise((resolve, reject) => {
        setTimeout(() => {
          resolve({ el: to.hash, top: 120 })
        }, wait_ms)
      })
    }

    if (to.meta.has_index) {
      // We are navigating to a page with index nav, that should be hidden automatically
      // Dependending on wether we have a tab navigation, add an offset to the "top" anker
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
