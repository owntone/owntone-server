import { createRouter, createWebHashHistory } from 'vue-router'
import PageAbout from '@/pages/PageAbout.vue'
import PageAlbum from '@/pages/PageAlbum.vue'
import PageAlbumSpotify from '@/pages/PageAlbumSpotify.vue'
import PageAlbums from '@/pages/PageAlbums.vue'
import PageArtist from '@/pages/PageArtist.vue'
import PageArtistSpotify from '@/pages/PageArtistSpotify.vue'
import PageArtistTracks from '@/pages/PageArtistTracks.vue'
import PageArtists from '@/pages/PageArtists.vue'
import PageAudiobookAlbum from '@/pages/PageAudiobookAlbum.vue'
import PageAudiobookAlbums from '@/pages/PageAudiobookAlbums.vue'
import PageAudiobookArtist from '@/pages/PageAudiobookArtist.vue'
import PageAudiobookArtists from '@/pages/PageAudiobookArtists.vue'
import PageAudiobookGenres from '@/pages/PageAudiobookGenres.vue'
import PageComposerAlbums from '@/pages/PageComposerAlbums.vue'
import PageComposerTracks from '@/pages/PageComposerTracks.vue'
import PageComposers from '@/pages/PageComposers.vue'
import PageFiles from '@/pages/PageFiles.vue'
import PageGenreAlbums from '@/pages/PageGenreAlbums.vue'
import PageGenreTracks from '@/pages/PageGenreTracks.vue'
import PageGenres from '@/pages/PageGenres.vue'
import PageMusic from '@/pages/PageMusic.vue'
import PageMusicRecentlyAdded from '@/pages/PageMusicRecentlyAdded.vue'
import PageMusicRecentlyPlayed from '@/pages/PageMusicRecentlyPlayed.vue'
import PageMusicSpotify from '@/pages/PageMusicSpotify.vue'
import PageMusicSpotifyFeaturedPlaylists from '@/pages/PageMusicSpotifyFeaturedPlaylists.vue'
import PageMusicSpotifyFollowedArtists from '@/pages/PageMusicSpotifyFollowedArtists.vue'
import PageMusicSpotifyNewReleases from '@/pages/PageMusicSpotifyNewReleases.vue'
import PageOutputs from '@/pages/PageOutputs.vue'
import PagePlayer from '@/pages/PagePlayer.vue'
import PagePlaylistFolder from '@/pages/PagePlaylistFolder.vue'
import PagePlaylistTracks from '@/pages/PagePlaylistTracks.vue'
import PagePlaylistTracksSpotify from '@/pages/PagePlaylistTracksSpotify.vue'
import PagePodcast from '@/pages/PagePodcast.vue'
import PagePodcasts from '@/pages/PagePodcasts.vue'
import PageQueue from '@/pages/PageQueue.vue'
import PageRadioStreams from '@/pages/PageRadioStreams.vue'
import PageSearchLibrary from '@/pages/PageSearchLibrary.vue'
import PageSearchSpotify from '@/pages/PageSearchSpotify.vue'
import PageSettingsArtwork from '@/pages/PageSettingsArtwork.vue'
import PageSettingsDevices from '@/pages/PageSettingsDevices.vue'
import PageSettingsOnlineServices from '@/pages/PageSettingsOnlineServices.vue'
import PageSettingsWebinterface from '@/pages/PageSettingsWebinterface.vue'

const TOP_WITH_TABS = 100

export const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    { path: '/:all(.*)*', redirect: '/' },
    { component: PageAbout, name: 'about', path: '/about' },
    { component: PageAlbum, name: 'music-album', path: '/music/albums/:id' },
    {
      component: PageAlbumSpotify,
      name: 'music-spotify-album',
      path: '/music/spotify/albums/:id'
    },
    {
      component: PageAlbums,
      name: 'music-albums',
      path: '/music/albums'
    },
    {
      component: PageArtist,
      name: 'music-artist',
      path: '/music/artists/:id'
    },
    {
      component: PageArtistSpotify,
      name: 'music-spotify-artist',
      path: '/music/spotify/artists/:id'
    },
    {
      component: PageArtists,
      name: 'music-artists',
      path: '/music/artists'
    },
    {
      component: PageArtistTracks,
      name: 'music-artist-tracks',
      path: '/music/artists/:id/tracks'
    },
    {
      component: PageAudiobookAlbum,
      name: 'audiobook-album',
      path: '/audiobook/albums/:id'
    },
    {
      component: PageAudiobookAlbums,
      name: 'audiobook-albums',
      path: '/audiobook/albums'
    },
    {
      component: PageAudiobookArtist,
      name: 'audiobook-artist',
      path: '/audiobook/artists/:id'
    },
    {
      component: PageAudiobookArtists,
      name: 'audiobook-artists',
      path: '/audiobook/artists'
    },
    {
      component: PageAudiobookGenres,
      name: 'audiobook-genres',
      path: '/audiobook/genres'
    },
    {
      name: 'audiobooks',
      path: '/audiobooks',
      redirect: { name: 'audiobook-artists' }
    },
    {
      component: PageOutputs,
      name: 'outputs',
      path: '/outputs'
    },
    {
      name: 'music',
      path: '/music',
      redirect: { name: 'music-history' }
    },
    {
      component: PageMusic,
      name: 'music-history',
      path: '/music/history'
    },
    {
      component: PageMusicRecentlyAdded,
      name: 'music-recently-added',
      path: '/music/recently-added'
    },
    {
      component: PageMusicRecentlyPlayed,
      name: 'music-recently-played',
      path: '/music/recently-played'
    },
    {
      component: PageMusicSpotify,
      name: 'music-spotify',
      path: '/music/spotify'
    },
    {
      component: PageMusicSpotifyFeaturedPlaylists,
      name: 'music-spotify-featured-playlists',
      path: '/music/spotify/featured-playlists'
    },
    {
      component: PageMusicSpotifyFollowedArtists,
      name: 'music-spotify-followed-artists',
      path: '/music/spotify/followed-artists'
    },
    {
      component: PageMusicSpotifyNewReleases,
      name: 'music-spotify-new-releases',
      path: '/music/spotify/new-releases'
    },
    {
      component: PageComposerAlbums,
      name: 'music-composer-albums',
      path: '/music/composers/:name/albums'
    },
    {
      component: PageComposerTracks,
      name: 'music-composer-tracks',
      path: '/music/composers/:name/tracks'
    },
    {
      component: PageComposers,
      name: 'music-composers',
      path: '/music/composers'
    },
    { component: PageFiles, name: 'files', path: '/files' },
    {
      component: PageGenreAlbums,
      name: 'genre-albums',
      path: '/genres/:name/albums'
    },
    {
      component: PageGenreTracks,
      name: 'genre-tracks',
      path: '/genres/:name/tracks'
    },
    {
      component: PageGenres,
      name: 'music-genres',
      path: '/music/genres'
    },
    { component: PagePlayer, name: 'player', path: '/player' },
    {
      name: 'playlists',
      path: '/playlists',
      redirect: { name: 'playlist-folder', params: { id: 0 } }
    },
    {
      component: PagePlaylistFolder,
      name: 'playlist-folder',
      path: '/playlists/:id'
    },
    {
      component: PagePlaylistTracks,
      name: 'playlist',
      path: '/playlists/:id/tracks'
    },
    {
      component: PagePlaylistTracksSpotify,
      name: 'playlist-spotify',
      path: '/playlists/spotify/:id/tracks'
    },
    { component: PagePodcast, name: 'podcast', path: '/podcasts/:id' },
    { component: PagePodcasts, name: 'podcasts', path: '/podcasts' },
    {
      component: PageRadioStreams,
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
      component: PageSettingsDevices,
      name: 'settings-devices',
      path: '/settings/devices'
    }
  ],
  scrollBehavior(to, from, savedPosition) {
    const delay = 0
    if (savedPosition) {
      // Use the saved scroll position (browser back/forward navigation)
      return new Promise((resolve) => {
        setTimeout(() => {
          resolve(savedPosition)
        }, delay)
      })
    }
    if (to.path === from.path && to.hash) {
      /*
       * Staying on the same page and jumping to an anchor (e. g. index nav)
       * As there is no transition, there is no timeout added
       */
      return { behavior: 'smooth', el: to.hash, top: TOP_WITH_TABS }
    }
    if (to.hash) {
      // We are navigating to an anchor of a new page, add a timeout to let the transition effect finish before scrolling
      return new Promise((resolve) => {
        setTimeout(() => {
          resolve({ el: to.hash, top: TOP_WITH_TABS })
        }, delay)
      })
    }
    return { left: 0, top: 0 }
  }
})
