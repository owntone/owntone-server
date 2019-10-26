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

    <tabs-search></tabs-search>

    <!-- Tracks -->
    <content-with-heading v-if="show_tracks">
      <template slot="heading-left">
        <p class="title is-4">Tracks</p>
      </template>
      <template slot="content">
        <list-item-track v-for="track in tracks.items" :key="track.id" :track="track" @click="play_track(track)">
          <template slot="actions">
            <a @click="open_track_dialog(track)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-track>
        <modal-dialog-track :show="show_track_details_modal" :track="selected_track" @close="show_track_details_modal = false" />
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
        <list-item-artist v-for="artist in artists.items" :key="artist.id" :artist="artist" @click="open_artist(artist)">
          <template slot="actions">
            <a @click="open_artist_dialog(artist)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-artist>
        <modal-dialog-artist :show="show_artist_details_modal" :artist="selected_artist" @close="show_artist_details_modal = false" />
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
        <list-item-album v-for="album in albums.items" :key="album.id" :album="album" @click="open_album(album)">
          <template slot="actions">
            <a @click="open_album_dialog(album)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-album>
        <modal-dialog-album :show="show_album_details_modal" :album="selected_album" @close="show_album_details_modal = false" />
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
        <list-item-playlist v-for="playlist in playlists.items" :key="playlist.id" :playlist="playlist" @click="open_playlist(playlist)">
          <template slot="actions">
            <a @click="open_playlist_dialog(playlist)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-playlist>
        <modal-dialog-playlist :show="show_playlist_details_modal" :playlist="selected_playlist" @close="show_playlist_details_modal = false" />
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
import ListItemTrack from '@/components/ListItemTrack'
import ListItemArtist from '@/components/ListItemArtist'
import ListItemAlbum from '@/components/ListItemAlbum'
import ListItemPlaylist from '@/components/ListItemPlaylist'
import ModalDialogTrack from '@/components/ModalDialogTrack'
import ModalDialogAlbum from '@/components/ModalDialogAlbum'
import ModalDialogArtist from '@/components/ModalDialogArtist'
import ModalDialogPlaylist from '@/components/ModalDialogPlaylist'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'

export default {
  name: 'PageSearch',
  components: { ContentWithHeading, TabsSearch, ListItemTrack, ListItemArtist, ListItemAlbum, ListItemPlaylist, ModalDialogTrack, ModalDialogAlbum, ModalDialogArtist, ModalDialogPlaylist },

  data () {
    return {
      search_query: '',
      tracks: { items: [], total: 0 },
      artists: { items: [], total: 0 },
      albums: { items: [], total: 0 },
      playlists: { items: [], total: 0 },

      show_track_details_modal: false,
      selected_track: {},

      show_album_details_modal: false,
      selected_album: {},

      show_artist_details_modal: false,
      selected_artist: {},

      show_playlist_details_modal: false,
      selected_playlist: {}
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
    search: function (route) {
      if (!route.query.query || route.query.query === '') {
        this.search_query = ''
        this.$refs.search_field.focus()
        return
      }

      var searchParams = {
        'type': route.query.type,
        'query': route.query.query,
        'media_kind': 'music'
      }

      if (route.query.limit) {
        searchParams.limit = route.query.limit
        searchParams.offset = route.query.offset
      }

      webapi.search(searchParams).then(({ data }) => {
        this.tracks = data.tracks ? data.tracks : { items: [], total: 0 }
        this.artists = data.artists ? data.artists : { items: [], total: 0 }
        this.albums = data.albums ? data.albums : { items: [], total: 0 }
        this.playlists = data.playlists ? data.playlists : { items: [], total: 0 }

        this.$store.commit(types.ADD_RECENT_SEARCH, searchParams.query)
      })
    },

    new_search: function () {
      if (!this.search_query) {
        return
      }

      this.$router.push({ path: '/search/library',
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
      this.$router.push({ path: '/search/library',
        query: {
          type: 'track',
          query: this.$route.query.query
        }
      })
    },

    open_search_artists: function () {
      this.$router.push({ path: '/search/library',
        query: {
          type: 'artist',
          query: this.$route.query.query
        }
      })
    },

    open_search_albums: function () {
      this.$router.push({ path: '/search/library',
        query: {
          type: 'album',
          query: this.$route.query.query
        }
      })
    },

    open_search_playlists: function () {
      this.$router.push({ path: '/search/library',
        query: {
          type: 'playlist',
          query: this.$route.query.query
        }
      })
    },

    play_track: function (track) {
      webapi.player_play_uri(track.uri, false)
    },

    open_artist: function (artist) {
      this.$router.push({ path: '/music/artists/' + artist.id })
    },

    open_album: function (album) {
      this.$router.push({ path: '/music/albums/' + album.id })
    },

    open_playlist: function (playlist) {
      this.$router.push({ path: '/playlists/' + playlist.id })
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
    }
  },

  mounted: function () {
    this.search(this.$route)
  },

  watch: {
    '$route' (to, from) {
      this.search(to)
    }
  }
}
</script>

<style>
</style>
