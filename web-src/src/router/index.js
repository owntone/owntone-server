import Vue from 'vue'
import VueRouter from 'vue-router'
import store from '@/store'
import * as types from '@/store/mutation_types'
import PageQueue from '@/pages/PageQueue'
import PageNowPlaying from '@/pages/PageNowPlaying'
import PageBrowse from '@/pages/PageBrowse'
import PageBrowseRecentlyAdded from '@/pages/PageBrowseRecentlyAdded'
import PageBrowseRecentlyPlayed from '@/pages/PageBrowseRecentlyPlayed'
import PageArtists from '@/pages/PageArtists'
import PageArtist from '@/pages/PageArtist'
import PageAlbums from '@/pages/PageAlbums'
import PageAlbum from '@/pages/PageAlbum'
import PageGenres from '@/pages/PageGenres'
import PageGenre from '@/pages/PageGenre'
import PageGenreTracks from '@/pages/PageGenreTracks'
import PageArtistTracks from '@/pages/PageArtistTracks'
import PageComposers from '@/pages/PageComposers'
import PageComposer from '@/pages/PageComposer'
import PageComposerTracks from '@/pages/PageComposerTracks'
import PagePodcasts from '@/pages/PagePodcasts'
import PagePodcast from '@/pages/PagePodcast'
import PageAudiobooksAlbums from '@/pages/PageAudiobooksAlbums'
import PageAudiobooksArtists from '@/pages/PageAudiobooksArtists'
import PageAudiobooksArtist from '@/pages/PageAudiobooksArtist'
import PageAudiobooksAlbum from '@/pages/PageAudiobooksAlbum'
import PagePlaylists from '@/pages/PagePlaylists'
import PagePlaylist from '@/pages/PagePlaylist'
import PageFiles from '@/pages/PageFiles'
import PageRadioStreams from '@/pages/PageRadioStreams'
import PageSearch from '@/pages/PageSearch'
import PageAbout from '@/pages/PageAbout'
import SpotifyPageBrowse from '@/pages/SpotifyPageBrowse'
import SpotifyPageBrowseNewReleases from '@/pages/SpotifyPageBrowseNewReleases'
import SpotifyPageBrowseFeaturedPlaylists from '@/pages/SpotifyPageBrowseFeaturedPlaylists'
import SpotifyPageArtist from '@/pages/SpotifyPageArtist'
import SpotifyPageAlbum from '@/pages/SpotifyPageAlbum'
import SpotifyPagePlaylist from '@/pages/SpotifyPagePlaylist'
import SpotifyPageSearch from '@/pages/SpotifyPageSearch'
import SettingsPageWebinterface from '@/pages/SettingsPageWebinterface'
import SettingsPageArtwork from '@/pages/SettingsPageArtwork'
import SettingsPageOnlineServices from '@/pages/SettingsPageOnlineServices'
import SettingsPageRemotesOutputs from '@/pages/SettingsPageRemotesOutputs'

Vue.use(VueRouter)

export const router = new VueRouter({
  routes: [
    {
      path: '/',
      name: 'PageQueue',
      component: PageQueue
    },
    {
      path: '/about',
      name: 'About',
      component: PageAbout
    },
    {
      path: '/now-playing',
      name: 'Now playing',
      component: PageNowPlaying
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
      meta: { show_progress: true, has_tabs: true, has_index: true }
    },
    {
      path: '/music/composers/:composer/tracks',
      name: 'ComposerTracks',
      component: PageComposerTracks,
      meta: { show_progress: true, has_tabs: true, has_index: true }
    },
    {
      path: '/podcasts',
      name: 'Podcasts',
      component: PagePodcasts,
      meta: { show_progress: true }
    },
    {
      path: '/podcasts/:album_id',
      name: 'Podcast',
      component: PagePodcast,
      meta: { show_progress: true }
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
      path: '/audiobooks/:album_id',
      name: 'Audiobook',
      component: PageAudiobooksAlbum,
      meta: { show_progress: true }
    },
    {
      path: '/radio',
      name: 'Radio',
      component: PageRadioStreams,
      meta: { show_progress: true }
    },
    {
      path: '/files',
      name: 'Files',
      component: PageFiles,
      meta: { show_progress: true }
    },
    {
      path: '/playlists',
      redirect: '/playlists/0'
    },
    {
      path: '/playlists/:playlist_id',
      name: 'Playlists',
      component: PagePlaylists,
      meta: { show_progress: true }
    },
    {
      path: '/playlists/:playlist_id/tracks',
      name: 'Playlist',
      component: PagePlaylist,
      meta: { show_progress: true }
    },
    {
      path: '/search',
      redirect: '/search/library'
    },
    {
      path: '/search/library',
      name: 'Search Library',
      component: PageSearch
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
      path: '/music/spotify/playlists/:playlist_id',
      name: 'Spotify Playlist',
      component: SpotifyPagePlaylist,
      meta: { show_progress: true }
    },
    {
      path: '/search/spotify',
      name: 'Spotify Search',
      component: SpotifyPageSearch
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
  scrollBehavior (to, from, savedPosition) {
    // console.log(to.path + '_' + from.path + '__' + to.hash + ' savedPosition:' + savedPosition)
    if (savedPosition) {
      return new Promise((resolve, reject) => {
        setTimeout(() => {
          resolve(savedPosition)
        }, 10)
      })
    } else if (to.path === from.path && to.hash) {
      return { selector: to.hash, offset: { x: 0, y: 120 } }
    } else if (to.hash) {
      return new Promise((resolve, reject) => {
        setTimeout(() => {
          resolve({ selector: to.hash, offset: { x: 0, y: 120 } })
        }, 10)
      })
    } else if (to.meta.has_index) {
      return new Promise((resolve, reject) => {
        setTimeout(() => {
          if (to.meta.has_tabs) {
            resolve({ selector: '#top', offset: { x: 0, y: 140 } })
          } else {
            resolve({ selector: '#top', offset: { x: 0, y: 100 } })
          }
        }, 10)
      })
    } else {
      return { x: 0, y: 0 }
    }
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
