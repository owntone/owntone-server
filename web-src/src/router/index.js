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
import PagePodcasts from '@/pages/PagePodcasts'
import PagePodcast from '@/pages/PagePodcast'
import PageAudiobooks from '@/pages/PageAudiobooks'
import PageAudiobook from '@/pages/PageAudiobook'
import PagePlaylists from '@/pages/PagePlaylists'
import PagePlaylist from '@/pages/PagePlaylist'
import PageSearch from '@/pages/PageSearch'
import PageAbout from '@/pages/PageAbout'
import SpotifyPageBrowse from '@/pages/SpotifyPageBrowse'
import SpotifyPageBrowseNewReleases from '@/pages/SpotifyPageBrowseNewReleases'
import SpotifyPageBrowseFeaturedPlaylists from '@/pages/SpotifyPageBrowseFeaturedPlaylists'
import SpotifyPageArtist from '@/pages/SpotifyPageArtist'
import SpotifyPageAlbum from '@/pages/SpotifyPageAlbum'
import SpotifyPagePlaylist from '@/pages/SpotifyPagePlaylist'
import SpotifyPageSearch from '@/pages/SpotifyPageSearch'

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
      meta: { show_progress: true }
    },
    {
      path: '/music/browse/recently_added',
      name: 'Browse Recently Added',
      component: PageBrowseRecentlyAdded,
      meta: { show_progress: true }
    },
    {
      path: '/music/browse/recently_played',
      name: 'Browse Recently Played',
      component: PageBrowseRecentlyPlayed,
      meta: { show_progress: true }
    },
    {
      path: '/music/artists',
      name: 'Artists',
      component: PageArtists,
      meta: { show_progress: true }
    },
    {
      path: '/music/artists/:artist_id',
      name: 'Artist',
      component: PageArtist,
      meta: { show_progress: true }
    },
    {
      path: '/music/albums',
      name: 'Albums',
      component: PageAlbums,
      meta: { show_progress: true }
    },
    {
      path: '/music/albums/:album_id',
      name: 'Album',
      component: PageAlbum,
      meta: { show_progress: true }
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
      name: 'Audiobooks',
      component: PageAudiobooks,
      meta: { show_progress: true }
    },
    {
      path: '/audiobooks/:album_id',
      name: 'Audiobook',
      component: PageAudiobook,
      meta: { show_progress: true }
    },
    {
      path: '/playlists',
      name: 'Playlists',
      component: PagePlaylists,
      meta: { show_progress: true }
    },
    {
      path: '/playlists/:playlist_id',
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
      meta: { show_progress: true }
    },
    {
      path: '/music/spotify/new-releases',
      name: 'Spotify Browse New Releases',
      component: SpotifyPageBrowseNewReleases,
      meta: { show_progress: true }
    },
    {
      path: '/music/spotify/featured-playlists',
      name: 'Spotify Browse Featured Playlists',
      component: SpotifyPageBrowseFeaturedPlaylists,
      meta: { show_progress: true }
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
      path: '/music/spotify/playlists/:user_id/:playlist_id',
      name: 'Spotify Playlist',
      component: SpotifyPagePlaylist,
      meta: { show_progress: true }
    },
    {
      path: '/search/spotify',
      name: 'Spotify Search',
      component: SpotifyPageSearch
    }
  ],
  scrollBehavior (to, from, savedPosition) {
    if (savedPosition) {
      return new Promise((resolve, reject) => {
        setTimeout(() => {
          resolve(savedPosition)
        }, 500)
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
  } else {
    next()
  }
})
