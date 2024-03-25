<template>
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
  <template v-for="type in search_types" :key="type">
    <content-with-heading v-if="show(type)" class="pt-0">
      <template #heading-left>
        <p class="title is-4" v-text="$t(`page.spotify.search.${type}s`)" />
      </template>
      <template #content>
        <component :is="components[type]" :items="results[type].items" />
        <VueEternalLoading v-if="query.type === type" :load="search_next">
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
        <nav v-if="show_all_button(type)" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search(type)"
              v-text="
                $t(`page.spotify.search.show-${type}s`, results[type].total, {
                  count: $filters.number(results[type].total)
                })
              "
            />
          </p>
        </nav>
        <p v-if="!results[type].total" class="has-text-centered-mobile">
          <i v-text="$t(`page.spotify.search.no-results`)" />
        </p>
      </template>
    </content-with-heading>
  </template>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListAlbumsSpotify from '@/components/ListAlbumsSpotify.vue'
import ListArtistsSpotify from '@/components/ListArtistsSpotify.vue'
import ListPlaylistsSpotify from '@/components/ListPlaylistsSpotify.vue'
import ListTracksSpotify from '@/components/ListTracksSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import TabsSearch from '@/components/TabsSearch.vue'
import { VueEternalLoading } from '@ts-pro/vue-eternal-loading'
import webapi from '@/webapi'

const PAGE_SIZE = 50

export default {
  name: 'PageSearchSpotify',
  components: {
    ContentWithHeading,
    ListAlbumsSpotify,
    ListArtistsSpotify,
    ListPlaylistsSpotify,
    ListTracksSpotify,
    TabsSearch,
    VueEternalLoading
  },

  data() {
    return {
      components: {
        album: ListAlbumsSpotify.name,
        artist: ListArtistsSpotify.name,
        playlist: ListPlaylistsSpotify.name,
        track: ListTracksSpotify.name
      },
      query: {},
      results: {
        album: { items: [], total: 0 },
        artist: { items: [], total: 0 },
        playlist: { items: [], total: 0 },
        track: { items: [], total: 0 }
      },
      search_param: {},
      search_query: '',
      search_types: ['track', 'artist', 'album', 'playlist']
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
    open_recent_search(query) {
      this.search_query = query
      this.new_search()
    },
    open_search(type) {
      this.$router.push({
        name: 'search-spotify',
        query: { query: this.$route.query.query, type }
      })
    },
    reset() {
      Object.entries(this.results).forEach(
        (key) => (this.results[key] = { items: [], total: 0 })
      )
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
    search_all() {
      const types = this.query.type
        .split(',')
        .filter((type) => this.search_types.includes(type))
      this.spotify_search(types).then((data) => {
        this.results.track = data.tracks ?? { items: [], total: 0 }
        this.results.artist = data.artists ?? { items: [], total: 0 }
        this.results.album = data.albums ?? { items: [], total: 0 }
        this.results.playlist = data.playlists ?? { items: [], total: 0 }
      })
    },
    search_next({ loaded }) {
      const items = this.results[this.query.type]
      this.spotify_search([this.query.type]).then((data) => {
        const [next] = Object.values(data)
        items.items.push(...next.items)
        items.total = next.total
        this.search_param.offset += next.limit
        loaded(next.items.length, PAGE_SIZE)
      })
    },
    show(type) {
      return this.$route.query.type?.includes(type) ?? false
    },
    show_all_button(type) {
      const items = this.results[type]
      return items.total > items.items.length
    },
    spotify_search(types) {
      return webapi.spotify().then(({ data }) => {
        this.search_param.market = data.webapi_country
        const api = new SpotifyWebApi()
        api.setAccessToken(data.webapi_token)
        return api.search(this.query.query, types, this.search_param)
      })
    }
  }
}
</script>

<style></style>
