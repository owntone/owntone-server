<template>
  <div>
    <!-- Search field + recent searches -->
    <section class="section fd-remove-padding-bottom">
      <div class="container">
        <div class="columns is-centered">
          <div class="column is-four-fifths">
            <form @submit.prevent="new_search">
              <div class="field">
                <p class="control is-expanded has-icons-left">
                  <input
                    ref="search_field"
                    v-model="search_query"
                    class="input is-rounded is-shadowless"
                    type="text"
                    placeholder="Search"
                    autocomplete="off"
                  />
                  <span class="icon is-left">
                    <i class="mdi mdi-magnify" />
                  </span>
                </p>
                <p class="help has-text-centered">
                  Tip: you can search by a smart playlist query language
                  <a
                    href="https://github.com/owntone/owntone-server/blob/master/README_SMARTPL.md"
                    target="_blank"
                    >expression</a
                  >
                  if you prefix it with <code>query:</code>.
                </p>
              </div>
            </form>
            <div class="tags" style="margin-top: 16px">
              <a
                v-for="recent_search in recent_searches"
                :key="recent_search"
                class="tag"
                @click="open_recent_search(recent_search)"
                >{{ recent_search }}</a
              >
            </div>
          </div>
        </div>
      </div>
    </section>

    <tabs-search :query="search_query" />

    <!-- Tracks -->
    <content-with-heading v-if="show_tracks && tracks.total">
      <template #heading-left>
        <p class="title is-4">Tracks</p>
      </template>
      <template #content>
        <list-tracks :tracks="tracks.items" />
      </template>
      <template #footer>
        <nav v-if="show_all_tracks_button" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search_tracks"
              >Show all {{ tracks.total.toLocaleString() }} tracks</a
            >
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_tracks && !tracks.total" class="mt-6">
      <template #content>
        <p><i>No tracks found</i></p>
      </template>
    </content-text>

    <!-- Artists -->
    <content-with-heading v-if="show_artists && artists.total">
      <template #heading-left>
        <p class="title is-4">Artists</p>
      </template>
      <template #content>
        <list-artists :artists="artists.items" />
      </template>
      <template #footer>
        <nav v-if="show_all_artists_button" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search_artists"
              >Show all {{ artists.total.toLocaleString() }} artists</a
            >
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_artists && !artists.total">
      <template #content>
        <p><i>No artists found</i></p>
      </template>
    </content-text>

    <!-- Albums -->
    <content-with-heading v-if="show_albums && albums.total">
      <template #heading-left>
        <p class="title is-4">Albums</p>
      </template>
      <template #content>
        <list-albums :albums="albums.items" />
      </template>
      <template #footer>
        <nav v-if="show_all_albums_button" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search_albums"
              >Show all {{ albums.total.toLocaleString() }} albums</a
            >
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_albums && !albums.total">
      <template #content>
        <p><i>No albums found</i></p>
      </template>
    </content-text>

    <!-- Composers -->
    <content-with-heading v-if="show_composers && composers.total">
      <template slot:heading-left>
        <p class="title is-4">Composers</p>
      </template>
      <template slot:content>
        <list-composers :composers="composers.items" />
      </template>
      <template slot:footer>
        <nav v-if="show_all_composers_button" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search_composers"
              >Show all {{ composers.total }} composers</a
            >
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_composers && !composers.total">
      <template slot:content>
        <p><i>No composers found</i></p>
      </template>
    </content-text>

    <!-- Playlists -->
    <content-with-heading v-if="show_playlists && playlists.total">
      <template #heading-left>
        <p class="title is-4">Playlists</p>
      </template>
      <template #content>
        <list-playlists :playlists="playlists.items" />
      </template>
      <template #footer>
        <nav v-if="show_all_playlists_button" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search_playlists"
              >Show all {{ playlists.total.toLocaleString() }} playlists</a
            >
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_playlists && !playlists.total">
      <template #content>
        <p><i>No playlists found</i></p>
      </template>
    </content-text>

    <!-- Podcasts -->
    <content-with-heading v-if="show_podcasts && podcasts.total">
      <template #heading-left>
        <p class="title is-4">Podcasts</p>
      </template>
      <template #content>
        <list-albums :albums="podcasts.items" />
      </template>
      <template #footer>
        <nav v-if="show_all_podcasts_button" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search_podcasts"
              >Show all {{ podcasts.total.toLocaleString() }} podcasts</a
            >
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_podcasts && !podcasts.total">
      <template #content>
        <p><i>No podcasts found</i></p>
      </template>
    </content-text>

    <!-- Audiobooks -->
    <content-with-heading v-if="show_audiobooks && audiobooks.total">
      <template #heading-left>
        <p class="title is-4">Audiobooks</p>
      </template>
      <template #content>
        <list-albums :albums="audiobooks.items" />
      </template>
      <template #footer>
        <nav v-if="show_all_audiobooks_button" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search_audiobooks"
              >Show all {{ audiobooks.total.toLocaleString() }} audiobooks</a
            >
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_audiobooks && !audiobooks.total">
      <template #content>
        <p><i>No audiobooks found</i></p>
      </template>
    </content-text>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ContentText from '@/templates/ContentText.vue'
import TabsSearch from '@/components/TabsSearch.vue'
import ListTracks from '@/components/ListTracks.vue'
import ListArtists from '@/components/ListArtists.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import ListComposers from '@/components/ListComposers.vue'
import ListPlaylists from '@/components/ListPlaylists.vue'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'

export default {
  name: 'PageSearch',
  components: {
    ContentWithHeading,
    ContentText,
    TabsSearch,
    ListTracks,
    ListArtists,
    ListAlbums,
    ListPlaylists,
    ListComposers
  },

  data() {
    return {
      search_query: '',

      tracks: { items: [], total: 0 },
      artists: { items: [], total: 0 },
      albums: { items: [], total: 0 },
      composers: { items: [], total: 0 },
      playlists: { items: [], total: 0 },
      audiobooks: { items: [], total: 0 },
      podcasts: { items: [], total: 0 }
    }
  },

  computed: {
    recent_searches() {
      return this.$store.state.recent_searches
    },

    show_tracks() {
      return this.$route.query.type && this.$route.query.type.includes('track')
    },
    show_all_tracks_button() {
      return this.tracks.total > this.tracks.items.length
    },

    show_artists() {
      return this.$route.query.type && this.$route.query.type.includes('artist')
    },
    show_all_artists_button() {
      return this.artists.total > this.artists.items.length
    },

    show_albums() {
      return this.$route.query.type && this.$route.query.type.includes('album')
    },
    show_all_albums_button() {
      return this.albums.total > this.albums.items.length
    },

    show_composers() {
      return (
        this.$route.query.type && this.$route.query.type.includes('composer')
      )
    },
    show_all_composers_button() {
      return this.composers.total > this.composers.items.length
    },

    show_playlists() {
      return (
        this.$route.query.type && this.$route.query.type.includes('playlist')
      )
    },
    show_all_playlists_button() {
      return this.playlists.total > this.playlists.items.length
    },

    show_audiobooks() {
      return (
        this.$route.query.type && this.$route.query.type.includes('audiobook')
      )
    },
    show_all_audiobooks_button() {
      return this.audiobooks.total > this.audiobooks.items.length
    },

    show_podcasts() {
      return (
        this.$route.query.type && this.$route.query.type.includes('podcast')
      )
    },
    show_all_podcasts_button() {
      return this.podcasts.total > this.podcasts.items.length
    },

    is_visible_artwork() {
      return this.$store.getters.settings_option(
        'webinterface',
        'show_cover_artwork_in_album_lists'
      ).value
    }
  },

  watch: {
    $route(to, from) {
      this.search(to)
    }
  },

  mounted: function () {
    this.search(this.$route)
  },

  methods: {
    search: function (route) {
      if (!route.query.query || route.query.query === '') {
        this.search_query = ''
        this.$refs.search_field.focus()
        return
      }

      this.search_query = route.query.query
      this.searchMusic(route.query)
      this.searchAudiobooks(route.query)
      this.searchPodcasts(route.query)
      this.$store.commit(types.ADD_RECENT_SEARCH, route.query.query)
    },

    searchMusic: function (query) {
      if (
        query.type.indexOf('track') < 0 &&
        query.type.indexOf('artist') < 0 &&
        query.type.indexOf('album') < 0 &&
        query.type.indexOf('playlist') < 0
      ) {
        return
      }

      const searchParams = {
        type: query.type,
        media_kind: 'music'
      }

      if (query.query.startsWith('query:')) {
        searchParams.expression = query.query.replace(/^query:/, '').trim()
      } else {
        searchParams.query = query.query
      }

      if (query.limit) {
        searchParams.limit = query.limit
        searchParams.offset = query.offset
      }

      webapi.search(searchParams).then(({ data }) => {
        this.tracks = data.tracks ? data.tracks : { items: [], total: 0 }
        this.artists = data.artists ? data.artists : { items: [], total: 0 }
        this.albums = data.albums ? data.albums : { items: [], total: 0 }
        this.composers = data.composers
          ? data.composers
          : { items: [], total: 0 }
        this.playlists = data.playlists
          ? data.playlists
          : { items: [], total: 0 }
      })
    },

    searchAudiobooks: function (query) {
      if (query.type.indexOf('audiobook') < 0) {
        return
      }

      const searchParams = {
        type: 'album',
        media_kind: 'audiobook'
      }

      if (query.query.startsWith('query:')) {
        searchParams.expression = query.query.replace(/^query:/, '').trim()
      } else {
        searchParams.expression =
          '((album includes "' +
          query.query +
          '" or artist includes "' +
          query.query +
          '") and media_kind is audiobook)'
      }

      if (query.limit) {
        searchParams.limit = query.limit
        searchParams.offset = query.offset
      }

      webapi.search(searchParams).then(({ data }) => {
        this.audiobooks = data.albums ? data.albums : { items: [], total: 0 }
      })
    },

    searchPodcasts: function (query) {
      if (query.type.indexOf('podcast') < 0) {
        return
      }

      const searchParams = {
        type: 'album',
        media_kind: 'podcast'
      }

      if (query.query.startsWith('query:')) {
        searchParams.expression = query.query.replace(/^query:/, '').trim()
      } else {
        searchParams.expression =
          '((album includes "' +
          query.query +
          '" or artist includes "' +
          query.query +
          '") and media_kind is podcast)'
      }

      if (query.limit) {
        searchParams.limit = query.limit
        searchParams.offset = query.offset
      }

      webapi.search(searchParams).then(({ data }) => {
        this.podcasts = data.albums ? data.albums : { items: [], total: 0 }
      })
    },

    new_search: function () {
      if (!this.search_query) {
        return
      }

      this.$router.push({
        path: '/search/library',
        query: {
          type: 'track,artist,album,playlist,audiobook,podcast,composer',
          query: this.search_query,
          limit: 3,
          offset: 0
        }
      })
      this.$refs.search_field.blur()
    },

    open_search_tracks: function () {
      this.$router.push({
        path: '/search/library',
        query: {
          type: 'track',
          query: this.$route.query.query
        }
      })
    },

    open_search_artists: function () {
      this.$router.push({
        path: '/search/library',
        query: {
          type: 'artist',
          query: this.$route.query.query
        }
      })
    },

    open_search_albums: function () {
      this.$router.push({
        path: '/search/library',
        query: {
          type: 'album',
          query: this.$route.query.query
        }
      })
    },

    open_search_composers: function () {
      this.$router.push({
        path: '/search/library',
        query: {
          type: 'tracks',
          query: this.$route.query.query
        }
      })
    },

    open_search_playlists: function () {
      this.$router.push({
        path: '/search/library',
        query: {
          type: 'playlist',
          query: this.$route.query.query
        }
      })
    },

    open_search_audiobooks: function () {
      this.$router.push({
        path: '/search/library',
        query: {
          type: 'audiobook',
          query: this.$route.query.query
        }
      })
    },

    open_search_podcasts: function () {
      this.$router.push({
        path: '/search/library',
        query: {
          type: 'podcast',
          query: this.$route.query.query
        }
      })
    },

    open_composer: function (composer) {
      this.$router.push({
        name: 'ComposerAlbums',
        params: { composer: composer.name }
      })
    },

    open_playlist: function (playlist) {
      this.$router.push({ path: '/playlists/' + playlist.id + '/tracks' })
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

    open_composer_dialog: function (composer) {
      this.selected_composer = composer
      this.show_composer_details_modal = true
    },

    open_playlist_dialog: function (playlist) {
      this.selected_playlist = playlist
      this.show_playlist_details_modal = true
    }
  }
}
</script>

<style></style>
