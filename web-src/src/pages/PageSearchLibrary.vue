<template>
  <div class="fd-page">
    <!-- Search field + recent searches -->
    <section class="section pb-0">
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
                    :placeholder="$t('page.search.placeholder')"
                    autocomplete="off"
                  />
                  <mdicon class="icon is-left" name="magnify" size="16" />
                </p>
                <i18n-t
                  tag="p"
                  class="help has-text-centered"
                  keypath="page.search.help"
                  scope="global"
                >
                  <template #query><code>query:</code></template>
                  <template #help
                    ><a
                      href="https://owntone.github.io/owntone-server/smart-playlists/"
                      target="_blank"
                      v-text="$t('page.search.expression')"
                  /></template>
                </i18n-t>
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
    <content-with-heading v-if="show_tracks && tracks.total" class="pt-0">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.search.tracks')" />
      </template>
      <template #content>
        <list-tracks :tracks="tracks" />
      </template>
      <template #footer>
        <nav v-if="show_all_tracks_button" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search_tracks"
              v-text="
                $t('page.search.show-tracks', tracks.total, {
                  count: $filters.number(tracks.total)
                })
              "
            />
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_tracks && !tracks.total" class="pt-0">
      <template #content>
        <p><i v-text="$t('page.search.no-tracks')" /></p>
      </template>
    </content-text>
    <!-- Artists -->
    <content-with-heading v-if="show_artists && artists.total">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.search.artists')" />
      </template>
      <template #content>
        <list-artists :artists="artists" :hide_group_title="true" />
      </template>
      <template #footer>
        <nav v-if="show_all_artists_button" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search_artists"
              v-text="
                $t('page.search.show-artists', artists.total, {
                  count: $filters.number(artists.total)
                })
              "
            />
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_artists && !artists.total">
      <template #content>
        <p><i v-text="$t('page.search.no-artists')" /></p>
      </template>
    </content-text>
    <!-- Albums -->
    <content-with-heading v-if="show_albums && albums.total">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.search.albums')" />
      </template>
      <template #content>
        <list-albums :albums="albums" :hide_group_title="true" />
      </template>
      <template #footer>
        <nav v-if="show_all_albums_button" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search_albums"
              v-text="
                $t('page.search.show-albums', albums.total, {
                  count: $filters.number(albums.total)
                })
              "
            />
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_albums && !albums.total">
      <template #content>
        <p><i v-text="$t('page.search.no-albums')" /></p>
      </template>
    </content-text>
    <!-- Composers -->
    <content-with-heading v-if="show_composers && composers.total">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.search.composers')" />
      </template>
      <template #content>
        <list-composers :composers="composers" />
      </template>
      <template #footer>
        <nav v-if="show_all_composers_button" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search_composers"
              v-text="
                $t('page.search.show-composers', composers.total, {
                  count: $filters.number(composers.total)
                })
              "
            />
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_composers && !composers.total">
      <template #content>
        <p><i v-text="$t('page.search.no-composers')" /></p>
      </template>
    </content-text>
    <!-- Playlists -->
    <content-with-heading v-if="show_playlists && playlists.total">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.search.playlists')" />
      </template>
      <template #content>
        <list-playlists :playlists="playlists" />
      </template>
      <template #footer>
        <nav v-if="show_all_playlists_button" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search_playlists"
              v-text="
                $t('page.search.show-playlists', playlists.total, {
                  count: $filters.number(playlists.total)
                })
              "
            />
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_playlists && !playlists.total">
      <template #content>
        <p><i v-text="$t('page.search.no-playlists')" /></p>
      </template>
    </content-text>
    <!-- Podcasts -->
    <content-with-heading v-if="show_podcasts && podcasts.total">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.search.podcasts')" />
      </template>
      <template #content>
        <list-albums :albums="podcasts" />
      </template>
      <template #footer>
        <nav v-if="show_all_podcasts_button" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search_podcasts"
              v-text="
                $t('page.search.show-podcasts', podcasts.total, {
                  count: $filters.number(podcasts.total)
                })
              "
            />
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_podcasts && !podcasts.total">
      <template #content>
        <p><i v-text="$t('page.search.no-podcasts')" /></p>
      </template>
    </content-text>

    <!-- Audiobooks -->
    <content-with-heading v-if="show_audiobooks && audiobooks.total">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.search.audiobooks')" />
      </template>
      <template #content>
        <list-albums :albums="audiobooks" />
      </template>
      <template #footer>
        <nav v-if="show_all_audiobooks_button" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search_audiobooks"
              v-text="
                $t('page.search.show-audiobooks', audiobooks.total, {
                  count: $filters.number(audiobooks.total)
                })
              "
            />
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-text v-if="show_audiobooks && !audiobooks.total">
      <template #content>
        <p><i v-text="$t('page.search.no-audiobooks')" /></p>
      </template>
    </content-text>
  </div>
</template>

<script>
import * as types from '@/store/mutation_types'
import ContentText from '@/templates/ContentText.vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupByList } from '@/lib/GroupByList'
import ListAlbums from '@/components/ListAlbums.vue'
import ListArtists from '@/components/ListArtists.vue'
import ListComposers from '@/components/ListComposers.vue'
import ListPlaylists from '@/components/ListPlaylists.vue'
import ListTracks from '@/components/ListTracks.vue'
import TabsSearch from '@/components/TabsSearch.vue'
import webapi from '@/webapi'

export default {
  name: 'PageSearchLibrary',
  components: {
    ContentText,
    ContentWithHeading,
    ListAlbums,
    ListArtists,
    ListComposers,
    ListPlaylists,
    ListTracks,
    TabsSearch
  },

  data() {
    return {
      search_query: '',
      tracks: new GroupByList(),
      artists: new GroupByList(),
      albums: new GroupByList(),
      composers: new GroupByList(),
      playlists: new GroupByList(),
      audiobooks: new GroupByList(),
      podcasts: new GroupByList()
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
    }
  },

  watch: {
    $route(to, from) {
      this.search(to)
    }
  },

  mounted() {
    this.search(this.$route)
  },

  methods: {
    search(route) {
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

    searchMusic(query) {
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
        this.tracks = new GroupByList(data.tracks)
        this.artists = new GroupByList(data.artists)
        this.albums = new GroupByList(data.albums)
        this.composers = new GroupByList(data.composers)
        this.playlists = new GroupByList(data.playlists)
      })
    },

    searchAudiobooks(query) {
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
        this.audiobooks = new GroupByList(data.albums)
      })
    },

    searchPodcasts(query) {
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
        this.podcasts = new GroupByList(data.albums)
      })
    },

    new_search() {
      if (!this.search_query) {
        return
      }

      this.$router.push({
        name: 'search-library',
        query: {
          type: 'track,artist,album,playlist,audiobook,podcast,composer',
          query: this.search_query,
          limit: 3,
          offset: 0
        }
      })
      this.$refs.search_field.blur()
    },

    open_search_tracks() {
      this.$router.push({
        name: 'search-library',
        query: {
          type: 'track',
          query: this.$route.query.query
        }
      })
    },

    open_search_artists() {
      this.$router.push({
        name: 'search-library',
        query: {
          type: 'artist',
          query: this.$route.query.query
        }
      })
    },

    open_search_albums() {
      this.$router.push({
        name: 'search-library',
        query: {
          type: 'album',
          query: this.$route.query.query
        }
      })
    },

    open_search_composers() {
      this.$router.push({
        name: 'search-library',
        query: {
          type: 'tracks',
          query: this.$route.query.query
        }
      })
    },

    open_search_playlists() {
      this.$router.push({
        name: 'search-library',
        query: {
          type: 'playlist',
          query: this.$route.query.query
        }
      })
    },

    open_search_audiobooks() {
      this.$router.push({
        name: 'search-library',
        query: {
          type: 'audiobook',
          query: this.$route.query.query
        }
      })
    },

    open_search_podcasts() {
      this.$router.push({
        name: 'search-library',
        query: {
          type: 'podcast',
          query: this.$route.query.query
        }
      })
    },

    open_composer(composer) {
      this.$router.push({
        name: 'music-composer-albums',
        params: { name: composer.name }
      })
    },

    open_playlist(playlist) {
      this.$router.push({ name: 'playlist', params: { id: playlist.id } })
    },

    open_recent_search(query) {
      this.search_query = query
      this.new_search()
    },

    open_track_dialog(track) {
      this.selected_track = track
      this.show_track_details_modal = true
    },

    open_album_dialog(album) {
      this.selected_album = album
      this.show_album_details_modal = true
    },

    open_artist_dialog(artist) {
      this.selected_artist = artist
      this.show_artist_details_modal = true
    },

    open_composer_dialog(composer) {
      this.selected_composer = composer
      this.show_composer_details_modal = true
    },

    open_playlist_dialog(playlist) {
      this.selected_playlist = playlist
      this.show_playlist_details_modal = true
    }
  }
}
</script>

<style></style>
