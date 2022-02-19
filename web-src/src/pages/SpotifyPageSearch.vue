<template>
  <div>
    <!-- Search field + recent searches -->
    <section class="section fd-remove-padding-bottom">
      <div class="container">
        <div class="columns is-centered">
          <div class="column is-four-fifths">
            <form v-on:submit.prevent="new_search">
              <div class="field">
                <p class="control is-expanded has-icons-left">
                  <input class="input is-rounded is-shadowless" type="text" placeholder="Search" v-model="search_query" ref="search_field" autocomplete="off">
                  <span class="icon is-left">
                    <i class="mdi mdi-magnify"></i>
                  </span>
                </p>
              </div>
            </form>
            <div class="tags" style="margin-top: 16px;">
              <a class="tag" v-for="recent_search in recent_searches" :key="recent_search" @click="open_recent_search(recent_search)">{{ recent_search }}</a>
            </div>
          </div>
        </div>
      </div>
    </section>

    <tabs-search :query="search_query"></tabs-search>

    <!-- Tracks -->
    <content-with-heading v-if="show_tracks && tracks.total">
      <template v-slot:heading-left>
        <p class="title is-4">Tracks</p>
      </template>
      <template v-slot:content>
        <spotify-list-item-track v-for="track in tracks.items" :key="track.id" :track="track" :album="track.album" :position="0" :context_uri="track.uri">
          <template v-slot:actions>
            <a @click="open_track_dialog(track)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </spotify-list-item-track>
        <VueEternalLoading v-if="query.type === 'track'" :load="search_tracks_next"><template #no-more>.</template></VueEternalLoading>
        <spotify-modal-dialog-track :show="show_track_details_modal" :track="selected_track" :album="selected_track.album" @close="show_track_details_modal = false" />
      </template>
      <template v-slot:footer>
        <nav v-if="show_all_tracks_button" class="level">
          <p class="level-item">
            <a class="button is-light is-small is-rounded" v-on:click="open_search_tracks">Show all {{ tracks.total.toLocaleString() }} tracks</a>
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_tracks && !tracks.total" class="mt-6">
      <template v-slot:content>
        <p><i>No tracks found</i></p>
      </template>
    </content-text>

    <!-- Artists -->
    <content-with-heading v-if="show_artists && artists.total">
      <template v-slot:heading-left>
        <p class="title is-4">Artists</p>
      </template>
      <template v-slot:content>
        <spotify-list-item-artist v-for="artist in artists.items" :key="artist.id" :artist="artist">
          <template v-slot:actions>
            <a @click="open_artist_dialog(artist)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </spotify-list-item-artist>
        <VueEternalLoading v-if="query.type === 'artist'" :load="search_artists_next"><template #no-more>.</template></VueEternalLoading>
        <spotify-modal-dialog-artist :show="show_artist_details_modal" :artist="selected_artist" @close="show_artist_details_modal = false" />
      </template>
      <template v-slot:footer>
        <nav v-if="show_all_artists_button" class="level">
          <p class="level-item">
            <a class="button is-light is-small is-rounded" v-on:click="open_search_artists">Show all {{ artists.total.toLocaleString() }} artists</a>
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_artists && !artists.total">
      <template v-slot:content>
        <p><i>No artists found</i></p>
      </template>
    </content-text>

    <!-- Albums -->
    <content-with-heading v-if="show_albums && albums.total">
      <template v-slot:heading-left>
        <p class="title is-4">Albums</p>
      </template>
      <template v-slot:content>
        <spotify-list-item-album v-for="album in albums.items"
            :key="album.id"
            :album="album"
            @click="open_album(album)">
          <template v-slot:artwork v-if="is_visible_artwork">
            <p class="image is-64x64 fd-has-shadow fd-has-action">
              <cover-artwork
                :artwork_url="artwork_url(album)"
                :artist="album.artist"
                :album="album.name"
                :maxwidth="64"
                :maxheight="64" />
            </p>
          </template>
          <template v-slot:actions>
            <a @click="open_album_dialog(album)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </spotify-list-item-album>
        <VueEternalLoading v-if="query.type === 'album'" :load="search_albums_next"><template #no-more>.</template></VueEternalLoading>
        <spotify-modal-dialog-album :show="show_album_details_modal" :album="selected_album" @close="show_album_details_modal = false" />
      </template>
      <template v-slot:footer>
        <nav v-if="show_all_albums_button" class="level">
          <p class="level-item">
            <a class="button is-light is-small is-rounded" v-on:click="open_search_albums">Show all {{ albums.total.toLocaleString() }} albums</a>
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_albums && !albums.total">
      <template v-slot:content>
        <p><i>No albums found</i></p>
      </template>
    </content-text>

    <!-- Playlists -->
    <content-with-heading v-if="show_playlists && playlists.total">
      <template v-slot:heading-left>
        <p class="title is-4">Playlists</p>
      </template>
      <template v-slot:content>
        <spotify-list-item-playlist v-for="playlist in playlists.items" :key="playlist.id" :playlist="playlist">
          <template v-slot:actions>
            <a @click="open_playlist_dialog(playlist)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </spotify-list-item-playlist>
        <VueEternalLoading v-if="query.type === 'playlist'" :load="search_playlists_next"><template #no-more>.</template></VueEternalLoading>
        <spotify-modal-dialog-playlist :show="show_playlist_details_modal" :playlist="selected_playlist" @close="show_playlist_details_modal = false" />
      </template>
      <template v-slot:footer>
        <nav v-if="show_all_playlists_button" class="level">
          <p class="level-item">
            <a class="button is-light is-small is-rounded" v-on:click="open_search_playlists">Show all {{ playlists.total.toLocaleString() }} playlists</a>
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_playlists && !playlists.total">
      <template v-slot:content>
        <p><i>No playlists found</i></p>
      </template>
    </content-text>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ContentText from '@/templates/ContentText.vue'
import TabsSearch from '@/components/TabsSearch.vue'
import SpotifyListItemTrack from '@/components/SpotifyListItemTrack.vue'
import SpotifyListItemArtist from '@/components/SpotifyListItemArtist.vue'
import SpotifyListItemAlbum from '@/components/SpotifyListItemAlbum.vue'
import SpotifyListItemPlaylist from '@/components/SpotifyListItemPlaylist.vue'
import SpotifyModalDialogTrack from '@/components/SpotifyModalDialogTrack.vue'
import SpotifyModalDialogArtist from '@/components/SpotifyModalDialogArtist.vue'
import SpotifyModalDialogAlbum from '@/components/SpotifyModalDialogAlbum.vue'
import SpotifyModalDialogPlaylist from '@/components/SpotifyModalDialogPlaylist.vue'
import CoverArtwork from '@/components/CoverArtwork.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'
import { VueEternalLoading } from '@ts-pro/vue-eternal-loading'

const PAGE_SIZE = 50

export default {
  name: 'SpotifyPageSearch',
  components: { ContentWithHeading, ContentText, TabsSearch, SpotifyListItemTrack, SpotifyListItemArtist, SpotifyListItemAlbum, SpotifyListItemPlaylist, SpotifyModalDialogTrack, SpotifyModalDialogArtist, SpotifyModalDialogAlbum, SpotifyModalDialogPlaylist, VueEternalLoading, CoverArtwork },

  data () {
    return {
      search_query: '',
      tracks: { items: [], total: 0 },
      artists: { items: [], total: 0 },
      albums: { items: [], total: 0 },
      playlists: { items: [], total: 0 },

      query: {},
      search_param: {},

      show_track_details_modal: false,
      selected_track: {},

      show_album_details_modal: false,
      selected_album: {},

      show_artist_details_modal: false,
      selected_artist: {},

      show_playlist_details_modal: false,
      selected_playlist: {},

      validSearchTypes: ['track', 'artist', 'album', 'playlist']
    }
  },

  computed: {
    recent_searches () {
      return this.$store.state.recent_searches.filter(search => !search.startsWith('query:'))
    },

    show_tracks () {
      return this.$route.query.type && this.$route.query.type.includes('track')
    },
    show_all_tracks_button () {
      return this.tracks.total > this.tracks.items.length
    },

    show_artists () {
      return this.$route.query.type && this.$route.query.type.includes('artist')
    },
    show_all_artists_button () {
      return this.artists.total > this.artists.items.length
    },

    show_albums () {
      return this.$route.query.type && this.$route.query.type.includes('album')
    },
    show_all_albums_button () {
      return this.albums.total > this.albums.items.length
    },

    show_playlists () {
      return this.$route.query.type && this.$route.query.type.includes('playlist')
    },
    show_all_playlists_button () {
      return this.playlists.total > this.playlists.items.length
    },

    is_visible_artwork () {
      return this.$store.getters.settings_option('webinterface', 'show_cover_artwork_in_album_lists').value
    }
  },

  methods: {
    reset: function () {
      this.tracks = { items: [], total: 0 }
      this.artists = { items: [], total: 0 }
      this.albums = { items: [], total: 0 }
      this.playlists = { items: [], total: 0 }
    },

    search: function () {
      this.reset()

      // If no search query present reset and focus search field
      if (!this.query.query || this.query.query === '' || this.query.query.startsWith('query:')) {
        this.search_query = ''
        this.$refs.search_field.focus()
        return
      }

      this.search_query = this.query.query
      this.search_param.limit = this.query.limit ? this.query.limit : PAGE_SIZE
      this.search_param.offset = this.query.offset ? this.query.offset : 0

      this.$store.commit(types.ADD_RECENT_SEARCH, this.query.query)

      this.search_all()
    },

    spotify_search: function () {
      return webapi.spotify().then(({ data }) => {
        this.search_param.market = data.webapi_country

        const spotifyApi = new SpotifyWebApi()
        spotifyApi.setAccessToken(data.webapi_token)

        const types = this.query.type.split(',').filter(type => this.validSearchTypes.includes(type))
        return spotifyApi.search(this.query.query, types, this.search_param)
      })
    },

    search_all: function () {
      this.spotify_search().then(data => {
        this.tracks = data.tracks ? data.tracks : { items: [], total: 0 }
        this.artists = data.artists ? data.artists : { items: [], total: 0 }
        this.albums = data.albums ? data.albums : { items: [], total: 0 }
        this.playlists = data.playlists ? data.playlists : { items: [], total: 0 }
      })
    },

    search_tracks_next: function ({ loaded }) {
      this.spotify_search().then(data => {
        this.tracks.items = this.tracks.items.concat(data.tracks.items)
        this.tracks.total = data.tracks.total
        this.search_param.offset += data.tracks.limit
        
        loaded(data.tracks.items.length, PAGE_SIZE)
      })
    },

    search_artists_next: function ({ loaded }) {
      this.spotify_search().then(data => {
        this.artists.items = this.artists.items.concat(data.artists.items)
        this.artists.total = data.artists.total
        this.search_param.offset += data.artists.limit
        
        loaded(data.artists.items.length, PAGE_SIZE)
      })
    },

    search_albums_next: function ({ loaded }) {
      this.spotify_search().then(data => {
        this.albums.items = this.albums.items.concat(data.albums.items)
        this.albums.total = data.albums.total
        this.search_param.offset += data.albums.limit
        
        loaded(data.albums.items.length, PAGE_SIZE)
      })
    },

    search_playlists_next: function ({ loaded }) {
      this.spotify_search().then(data => {
        this.playlists.items = this.playlists.items.concat(data.playlists.items)
        this.playlists.total = data.playlists.total
        this.search_param.offset += data.playlists.limit
        
        loaded(data.playlists.items.length, PAGE_SIZE)
      })
    },

    new_search: function () {
      if (!this.search_query) {
        return
      }

      this.$router.push({
        path: '/search/spotify',
        query: {
          type: 'track,artist,album,playlist,audiobook,podcast',
          query: this.search_query,
          limit: 3,
          offset: 0
        }
      })
      this.$refs.search_field.blur()
    },

    open_search_tracks: function () {
      this.$router.push({
        path: '/search/spotify',
        query: {
          type: 'track',
          query: this.$route.query.query
        }
      })
    },

    open_search_artists: function () {
      this.$router.push({
        path: '/search/spotify',
        query: {
          type: 'artist',
          query: this.$route.query.query
        }
      })
    },

    open_search_albums: function () {
      this.$router.push({
        path: '/search/spotify',
        query: {
          type: 'album',
          query: this.$route.query.query
        }
      })
    },

    open_search_playlists: function () {
      this.$router.push({
        path: '/search/spotify',
        query: {
          type: 'playlist',
          query: this.$route.query.query
        }
      })
    },

    open_recent_search: function (query) {
      this.search_query = query
      this.new_search()
    },

    open_track_dialog: function (track) {
      this.selected_track = track
      this.show_track_details_modal = true
    },

    open_album_dialog: function (album) {
      this.selected_album = album
      this.show_album_details_modal = true
    },

    open_artist_dialog: function (artist) {
      this.selected_artist = artist
      this.show_artist_details_modal = true
    },

    open_playlist_dialog: function (playlist) {
      this.selected_playlist = playlist
      this.show_playlist_details_modal = true
    },

    open_album: function (album) {
      this.$router.push({ path: '/music/spotify/albums/' + album.id })
    },

    artwork_url: function (album) {
      if (album.images && album.images.length > 0) {
        return album.images[0].url
      }
      return ''
    }
  },

  mounted: function () {
    this.query = this.$route.query
    this.search()
  },

  watch: {
    '$route' (to, from) {
      this.query = to.query
      this.search()
    }
  }
}
</script>

<style>
</style>
