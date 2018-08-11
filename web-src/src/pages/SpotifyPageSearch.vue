<template>
  <div>
    <!-- Search field + recent searches -->
    <section class="section fd-tabs-section">
      <div class="container">
        <div class="columns is-centered">
          <div class="column is-four-fifths">
            <form v-on:submit.prevent="new_search">
              <div class="field">
                <p class="control is-expanded has-icons-left">
                  <input class="input is-rounded is-shadowless" type="text" placeholder="Search" v-model="search_query" ref="search_field">
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

    <tabs-search></tabs-search>

    <!-- Tracks -->
    <content-with-heading v-if="show_tracks">
      <template slot="heading-left">
        <p class="title is-4">Tracks</p>
      </template>
      <template slot="content">
        <spotify-list-item-track v-for="track in tracks.items" :key="track.id" :track="track" :album="track.album" :position="0" :context_uri="track.uri"></spotify-list-item-track>
        <infinite-loading v-if="query.type === 'track'" @infinite="search_tracks_next"><span slot="no-more">.</span></infinite-loading>
      </template>
      <template slot="footer">
        <nav v-if="show_all_tracks_button" class="level">
          <p class="level-item">
            <a class="button is-light is-small is-rounded" v-on:click="open_search_tracks">Show all {{ tracks.total }} tracks</a>
          </p>
        </nav>
        <p v-if="!tracks.total">No results</p>
      </template>
    </content-with-heading>

    <!-- Artists -->
    <content-with-heading v-if="show_artists">
      <template slot="heading-left">
        <p class="title is-4">Artists</p>
      </template>
      <template slot="content">
        <spotify-list-item-artist v-for="artist in artists.items" :key="artist.id" :artist="artist"></spotify-list-item-artist>
        <infinite-loading v-if="query.type === 'artist'" @infinite="search_artists_next"><span slot="no-more">.</span></infinite-loading>
      </template>
      <template slot="footer">
        <nav v-if="show_all_artists_button" class="level">
          <p class="level-item">
            <a class="button is-light is-small is-rounded" v-on:click="open_search_artists">Show all {{ artists.total }} artists</a>
          </p>
        </nav>
        <p v-if="!artists.total">No results</p>
      </template>
    </content-with-heading>

    <!-- Albums -->
    <content-with-heading v-if="show_albums">
      <template slot="heading-left">
        <p class="title is-4">Albums</p>
      </template>
      <template slot="content">
        <spotify-list-item-album v-for="album in albums.items" :key="album.id" :album="album"></spotify-list-item-album>
        <infinite-loading v-if="query.type === 'album'" @infinite="search_albums_next"><span slot="no-more">.</span></infinite-loading>
      </template>
      <template slot="footer">
        <nav v-if="show_all_albums_button" class="level">
          <p class="level-item">
            <a class="button is-light is-small is-rounded" v-on:click="open_search_albums">Show all {{ albums.total }} albums</a>
          </p>
        </nav>
        <p v-if="!albums.total">No results</p>
      </template>
    </content-with-heading>

    <!-- Playlists -->
    <content-with-heading v-if="show_playlists">
      <template slot="heading-left">
        <p class="title is-4">Playlists</p>
      </template>
      <template slot="content">
        <spotify-list-item-playlist v-for="playlist in playlists.items" :key="playlist.id" :playlist="playlist"></spotify-list-item-playlist>
        <infinite-loading v-if="query.type === 'playlist'" @infinite="search_playlists_next"><span slot="no-more">.</span></infinite-loading>
      </template>
      <template slot="footer">
        <nav v-if="show_all_playlists_button" class="level">
          <p class="level-item">
            <a class="button is-light is-small is-rounded" v-on:click="open_search_playlists">Show all {{ playlists.total }} playlists</a>
          </p>
        </nav>
        <p v-if="!playlists.total">No results</p>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsSearch from '@/components/TabsSearch'
import SpotifyListItemTrack from '@/components/SpotifyListItemTrack'
import SpotifyListItemArtist from '@/components/SpotifyListItemArtist'
import SpotifyListItemAlbum from '@/components/SpotifyListItemAlbum'
import SpotifyListItemPlaylist from '@/components/SpotifyListItemPlaylist'
import SpotifyWebApi from 'spotify-web-api-js'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'
import InfiniteLoading from 'vue-infinite-loading'

export default {
  name: 'SpotifyPageSearch',
  components: { ContentWithHeading, TabsSearch, SpotifyListItemTrack, SpotifyListItemArtist, SpotifyListItemAlbum, SpotifyListItemPlaylist, InfiniteLoading },

  data () {
    return {
      search_query: '',
      tracks: { items: [], total: 0 },
      artists: { items: [], total: 0 },
      albums: { items: [], total: 0 },
      playlists: { items: [], total: 0 },

      query: {},
      search_param: {}
    }
  },

  computed: {
    recent_searches () {
      return this.$store.state.recent_searches
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
      if (!this.query.query || this.query.query === '') {
        this.search_query = ''
        this.$refs.search_field.focus()
        return
      }

      this.search_param.limit = this.query.limit ? this.query.limit : 50
      this.search_param.offset = this.query.offset ? this.query.offset : 0

      this.$store.commit(types.ADD_RECENT_SEARCH, this.query.query)

      if (this.query.type.includes(',')) {
        this.search_all()
      }
    },

    spotify_search: function () {
      return webapi.spotify().then(({ data }) => {
        this.search_param.market = data.webapi_country

        var spotifyApi = new SpotifyWebApi()
        spotifyApi.setAccessToken(data.webapi_token)

        return spotifyApi.search(this.query.query, this.query.type.split(','), this.search_param)
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

    search_tracks_next: function ($state) {
      this.spotify_search().then(data => {
        this.tracks.items = this.tracks.items.concat(data.tracks.items)
        this.tracks.total = data.tracks.total
        this.search_param.offset += data.tracks.limit

        $state.loaded()
        if (this.search_param.offset >= this.tracks.total) {
          $state.complete()
        }
      })
    },

    search_artists_next: function ($state) {
      this.spotify_search().then(data => {
        this.artists.items = this.artists.items.concat(data.artists.items)
        this.artists.total = data.artists.total
        this.search_param.offset += data.artists.limit

        $state.loaded()
        if (this.search_param.offset >= this.artists.total) {
          $state.complete()
        }
      })
    },

    search_albums_next: function ($state) {
      this.spotify_search().then(data => {
        this.albums.items = this.albums.items.concat(data.albums.items)
        this.albums.total = data.albums.total
        this.search_param.offset += data.albums.limit

        $state.loaded()
        if (this.search_param.offset >= this.albums.total) {
          $state.complete()
        }
      })
    },

    search_playlists_next: function ($state) {
      this.spotify_search().then(data => {
        this.playlists.items = this.playlists.items.concat(data.playlists.items)
        this.playlists.total = data.playlists.total
        this.search_param.offset += data.playlists.limit

        $state.loaded()
        if (this.search_param.offset >= this.playlists.total) {
          $state.complete()
        }
      })
    },

    new_search: function () {
      if (!this.search_query) {
        return
      }

      this.$router.push({ path: '/search/spotify',
        query: {
          type: 'track,artist,album,playlist',
          query: this.search_query,
          limit: 3,
          offset: 0
        }
      })
      this.$refs.search_field.blur()
    },

    open_search_tracks: function () {
      this.$router.push({ path: '/search/spotify',
        query: {
          type: 'track',
          query: this.$route.query.query
        }
      })
    },

    open_search_artists: function () {
      this.$router.push({ path: '/search/spotify',
        query: {
          type: 'artist',
          query: this.$route.query.query
        }
      })
    },

    open_search_albums: function () {
      this.$router.push({ path: '/search/spotify',
        query: {
          type: 'album',
          query: this.$route.query.query
        }
      })
    },

    open_search_playlists: function () {
      this.$router.push({ path: '/search/spotify',
        query: {
          type: 'playlist',
          query: this.$route.query.query
        }
      })
    },

    open_recent_search: function (query) {
      this.search_query = query
      this.new_search()
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
