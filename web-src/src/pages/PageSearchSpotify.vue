<template>
  <div>
    <!-- Search field + recent searches -->
    <section class="section pb-0">
      <div class="container">
        <div class="columns is-centered">
          <div class="column is-four-fifths">
            <form @submit.prevent="new_search">
              <div class="field">
                <p class="control has-icons-left">
                  <input
                    ref="search_field"
                    v-model="search_query"
                    class="input is-rounded is-shadowless"
                    type="text"
                    :placeholder="$t('page.spotify.search.placeholder')"
                    autocomplete="off"
                  />
                  <mdicon class="icon is-left" name="magnify" size="16" />
                </p>
              </div>
            </form>
            <div class="tags mt-4">
              <a
                v-for="recent_search in recent_searches"
                :key="recent_search"
                class="tag"
                @click="open_recent_search(recent_search)"
                v-text="recent_search"
              />
            </div>
          </div>
        </div>
      </div>
    </section>
    <tabs-search :query="search_query" />
    <!-- Tracks -->
    <content-with-heading v-if="show('track') && tracks.total" class="pt-0">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.spotify.search.tracks')" />
      </template>
      <template #content>
        <list-item-track-spotify
          v-for="track in tracks.items"
          :key="track.id"
          :item="track"
          :position="0"
          :context_uri="track.uri"
        >
          <template #actions>
            <a @click.prevent.stop="open_track_dialog(track)">
              <mdicon
                class="icon has-text-dark"
                name="dots-vertical"
                size="16"
              />
            </a>
          </template>
        </list-item-track-spotify>
        <VueEternalLoading
          v-if="query.type === 'track'"
          :load="search_tracks_next"
        >
          <template #loading>
            <div class="columns is-centered">
              <div class="column has-text-centered">
                <mdicon class="icon mdi-spin" name="loading" />
              </div>
            </div>
          </template>
          <template #no-more>&nbsp;</template>
        </VueEternalLoading>
        <modal-dialog-track-spotify
          :show="show_track_details_modal"
          :track="selected_track"
          :album="selected_track.album"
          @close="show_track_details_modal = false"
        />
      </template>
      <template #footer>
        <nav v-if="show_all_button(tracks)" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search('track')"
              v-text="
                $t('page.spotify.search.show-all-tracks', tracks.total, {
                  count: $filters.number(tracks.total)
                })
              "
            />
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show('track') && !tracks.total" class="pt-0">
      <template #content>
        <p><i v-text="$t('page.spotify.search.no-tracks')" /></p>
      </template>
    </content-text>
    <!-- Artists -->
    <content-with-heading v-if="show('artist') && artists.total">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.spotify.search.artists')" />
      </template>
      <template #content>
        <list-item-artist-spotify
          v-for="artist in artists.items"
          :key="artist.id"
          :item="artist"
        >
          <template #actions>
            <a @click.prevent.stop="open_artist_dialog(artist)">
              <mdicon
                class="icon has-text-dark"
                name="dots-vertical"
                size="16"
              />
            </a>
          </template>
        </list-item-artist-spotify>
        <VueEternalLoading
          v-if="query.type === 'artist'"
          :load="search_artists_next"
        >
          <template #loading>
            <div class="columns is-centered">
              <div class="column has-text-centered">
                <mdicon class="icon mdi-spin" name="loading" />
              </div>
            </div>
          </template>
          <template #no-more>&nbsp;</template>
        </VueEternalLoading>
        <modal-dialog-artist-spotify
          :show="show_artist_details_modal"
          :artist="selected_artist"
          @close="show_artist_details_modal = false"
        />
      </template>
      <template #footer>
        <nav v-if="show_all_button(artists)" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search('artist')"
              v-text="
                $t('page.spotify.search.show-all-artists', artists.total, {
                  count: $filters.number(artists.total)
                })
              "
            />
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show('artist') && !artists.total">
      <template #content>
        <p><i v-text="$t('page.spotify.search.no-artists')" /></p>
      </template>
    </content-text>
    <!-- Albums -->
    <content-with-heading v-if="show('album') && albums.total">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.spotify.search.albums')" />
      </template>
      <template #content>
        <list-item-album-spotify
          v-for="album in albums.items"
          :key="album.id"
          :item="album"
        >
        </list-item-album-spotify>
        <VueEternalLoading
          v-if="query.type === 'album'"
          :load="search_albums_next"
        >
          <template #loading>
            <div class="columns is-centered">
              <div class="column has-text-centered">
                <mdicon class="icon mdi-spin" name="loading" />
              </div>
            </div>
          </template>
          <template #no-more>&nbsp;</template>
        </VueEternalLoading>
      </template>
      <template #footer>
        <nav v-if="show_all_button(albums)" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search('album')"
              v-text="
                $t('page.spotify.search.show-all-albums', albums.total, {
                  count: $filters.number(albums.total)
                })
              "
            />
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show('album') && !albums.total">
      <template #content>
        <p><i v-text="$t('page.spotify.search.no-albums')" /></p>
      </template>
    </content-text>
    <!-- Playlists -->
    <content-with-heading v-if="show('playlist') && playlists.total">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.spotify.search.playlists')" />
      </template>
      <template #content>
        <list-item-playlist-spotify
          v-for="playlist in playlists.items"
          :key="playlist.id"
          :item="playlist"
        >
        </list-item-playlist-spotify>
        <VueEternalLoading
          v-if="query.type === 'playlist'"
          :load="search_playlists_next"
        >
          <template #loading>
            <div class="columns is-centered">
              <div class="column has-text-centered">
                <mdicon class="icon mdi-spin" name="loading" />
              </div>
            </div>
          </template>
          <template #no-more>&nbsp;</template>
        </VueEternalLoading>
      </template>
      <template #footer>
        <nav v-if="show_all_button(playlists)" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search('playlist')"
              v-text="
                $t('page.spotify.search.show-all-playlists', playlists.total, {
                  count: $filters.number(playlists.total)
                })
              "
            />
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show('playlist') && !playlists.total">
      <template #content>
        <p><i v-text="$t('page.spotify.search.no-playlists')" /></p>
      </template>
    </content-text>
  </div>
</template>

<script>
import ContentText from '@/templates/ContentText.vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListItemAlbumSpotify from '@/components/ListItemAlbumSpotify.vue'
import ListItemArtistSpotify from '@/components/ListItemArtistSpotify.vue'
import ListItemPlaylistSpotify from '@/components/ListItemPlaylistSpotify.vue'
import ListItemTrackSpotify from '@/components/ListItemTrackSpotify.vue'
import ModalDialogArtistSpotify from '@/components/ModalDialogArtistSpotify.vue'
import ModalDialogTrackSpotify from '@/components/ModalDialogTrackSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import TabsSearch from '@/components/TabsSearch.vue'
import { VueEternalLoading } from '@ts-pro/vue-eternal-loading'
import webapi from '@/webapi'

const PAGE_SIZE = 50

export default {
  name: 'PageSearchSpotify',
  components: {
    ContentText,
    ContentWithHeading,
    ListItemAlbumSpotify,
    ListItemArtistSpotify,
    ListItemPlaylistSpotify,
    ListItemTrackSpotify,
    ModalDialogArtistSpotify,
    ModalDialogTrackSpotify,
    TabsSearch,
    VueEternalLoading
  },

  data() {
    return {
      albums: { items: [], total: 0 },
      artists: { items: [], total: 0 },
      playlists: { items: [], total: 0 },
      query: {},
      search_param: {},
      search_query: '',
      selected_artist: {},
      selected_track: {},
      show_artist_details_modal: false,
      show_track_details_modal: false,
      tracks: { items: [], total: 0 },
      validSearchTypes: ['track', 'artist', 'album', 'playlist']
    }
  },

  computed: {
    recent_searches() {
      return this.$store.state.recent_searches.filter(
        (search) => !search.startsWith('query:')
      )
    }
  },

  watch: {
    $route(to, from) {
      this.query = to.query
      this.search()
    }
  },

  mounted() {
    this.query = this.$route.query
    this.search()
  },

  methods: {
    new_search() {
      if (!this.search_query) {
        return
      }
      this.$router.push({
        name: 'search-spotify',
        query: {
          limit: 3,
          offset: 0,
          query: this.search_query,
          type: 'track,artist,album,playlist,audiobook,podcast'
        }
      })
      this.$refs.search_field.blur()
    },
    open_artist_dialog(artist) {
      this.selected_artist = artist
      this.show_artist_details_modal = true
    },
    open_recent_search(query) {
      this.search_query = query
      this.new_search()
    },
    open_search(type) {
      this.$router.push({
        name: 'search-spotify',
        query: {
          query: this.$route.query.query,
          type
        }
      })
    },
    open_track_dialog(track) {
      this.selected_track = track
      this.show_track_details_modal = true
    },
    reset() {
      this.tracks = { items: [], total: 0 }
      this.artists = { items: [], total: 0 }
      this.albums = { items: [], total: 0 }
      this.playlists = { items: [], total: 0 }
    },
    search() {
      this.reset()
      this.search_query = this.query.query?.trim()
      if (!this.search_query || this.search_query.startsWith('query:')) {
        this.search_query = ''
        this.$refs.search_field.focus()
        return
      }
      this.query.query = this.search_query
      this.search_param.limit = this.query.limit ?? PAGE_SIZE
      this.search_param.offset = this.query.offset ?? 0
      this.$store.dispatch('add_recent_search', this.query.query)
      this.search_all()
    },
    search_albums_next({ loaded }) {
      this.spotify_search().then((data) => {
        this.albums.items = this.albums.items.concat(data.albums.items)
        this.albums.total = data.albums.total
        this.search_param.offset += data.albums.limit
        loaded(data.albums.items.length, PAGE_SIZE)
      })
    },
    search_all() {
      this.spotify_search().then((data) => {
        this.tracks = data.tracks ?? { items: [], total: 0 }
        this.artists = data.artists ?? { items: [], total: 0 }
        this.albums = data.albums ?? { items: [], total: 0 }
        this.playlists = data.playlists ?? { items: [], total: 0 }
      })
    },
    search_artists_next({ loaded }) {
      this.spotify_search().then((data) => {
        this.artists.items = this.artists.items.concat(data.artists.items)
        this.artists.total = data.artists.total
        this.search_param.offset += data.artists.limit
        loaded(data.artists.items.length, PAGE_SIZE)
      })
    },
    search_playlists_next({ loaded }) {
      this.spotify_search().then((data) => {
        this.playlists.items = this.playlists.items.concat(data.playlists.items)
        this.playlists.total = data.playlists.total
        this.search_param.offset += data.playlists.limit
        loaded(data.playlists.items.length, PAGE_SIZE)
      })
    },
    search_tracks_next({ loaded }) {
      this.spotify_search().then((data) => {
        this.tracks.items = this.tracks.items.concat(data.tracks.items)
        this.tracks.total = data.tracks.total
        this.search_param.offset += data.tracks.limit
        loaded(data.tracks.items.length, PAGE_SIZE)
      })
    },
    show(type) {
      return this.$route.query.type?.includes(type) ?? false
    },
    show_all_button(items) {
      return items.total > items.items.length
    },
    spotify_search() {
      return webapi.spotify().then(({ data }) => {
        this.search_param.market = data.webapi_country
        const spotifyApi = new SpotifyWebApi()
        spotifyApi.setAccessToken(data.webapi_token)
        const types = this.query.type
          .split(',')
          .filter((type) => this.validSearchTypes.includes(type))
        return spotifyApi.search(this.query.query, types, this.search_param)
      })
    }
  }
}
</script>

<style></style>
