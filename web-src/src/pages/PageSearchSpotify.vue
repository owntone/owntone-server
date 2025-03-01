<template>
  <section class="section pb-0">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <form @submit.prevent="search">
            <div class="field">
              <p class="control has-icons-left">
                <input
                  ref="search_field"
                  v-model="search_query"
                  class="input is-rounded"
                  type="text"
                  :placeholder="$t('page.spotify.search.placeholder')"
                  autocomplete="off"
                />
                <mdicon class="icon is-left" name="magnify" size="16" />
              </p>
            </div>
          </form>
          <div class="field is-grouped is-grouped-multiline mt-4">
            <div v-for="query in recent_searches" :key="query" class="control">
              <div class="tags has-addons">
                <a class="tag" @click="open_search(query)" v-text="query" />
                <a class="tag is-delete" @click="remove_search(query)" />
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </section>
  <tabs-search @search-library="search_library" @search-spotify="search" />
  <template v-for="[type, items] in results" :key="type">
    <content-with-heading>
      <template #heading-left>
        <p class="title is-4" v-text="$t(`page.spotify.search.${type}s`)" />
      </template>
      <template #content>
        <component :is="components[type]" :items="items.items" />
        <vue-eternal-loading v-if="expanded" :load="search_next">
          <template #loading>
            <div class="columns is-centered">
              <div class="column has-text-centered">
                <mdicon class="icon mdi-spin" name="loading" />
              </div>
            </div>
          </template>
          <template #no-more>
            <br />
          </template>
        </vue-eternal-loading>
      </template>
      <template v-if="!expanded" #footer>
        <nav v-if="show_all_button(items)" class="level">
          <p class="level-item">
            <a
              class="button is-small is-rounded"
              @click="expand(type)"
              v-text="
                $t(
                  `page.spotify.search.show-${type}s`,
                  { count: `${$n(items.total)}` },
                  items.total
                )
              "
            />
          </p>
        </nav>
        <p v-if="!items.total" class="has-text-centered-mobile">
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
import { useSearchStore } from '@/stores/search'
import webapi from '@/webapi'

const PAGE_SIZE = 3,
  PAGE_SIZE_EXPANDED = 50,
  SEARCH_TYPES = ['track', 'artist', 'album', 'playlist']

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

  setup() {
    return { searchStore: useSearchStore() }
  },

  data() {
    return {
      components: {
        album: ListAlbumsSpotify.name,
        artist: ListArtistsSpotify.name,
        playlist: ListPlaylistsSpotify.name,
        track: ListTracksSpotify.name
      },
      results: new Map(),
      search_parameters: {},
      search_query: '',
      search_types: SEARCH_TYPES
    }
  },

  computed: {
    expanded() {
      return this.search_types.length === 1
    },
    recent_searches() {
      return this.searchStore.recent_searches.filter(
        (query) => !query.startsWith('query:')
      )
    }
  },

  watch: {
    search_query() {
      this.searchStore.search_query = this.search_query
    }
  },

  mounted() {
    this.searchStore.search_source = this.$route.name
    this.search_query = this.searchStore.search_query
    this.search_parameters.limit = PAGE_SIZE
    this.search()
  },

  methods: {
    expand(type) {
      this.search_query = this.searchStore.search_query
      this.search_types = [type]
      this.search_parameters.limit = PAGE_SIZE_EXPANDED
      this.search_parameters.offset = 0
      this.search()
    },
    open_search(query) {
      this.search_query = query
      this.search_types = SEARCH_TYPES
      this.search_parameters.limit = PAGE_SIZE
      this.search_parameters.offset = 0
      this.search()
    },
    remove_search(query) {
      this.searchStore.remove(query)
    },
    reset() {
      this.results.clear()
      this.search_types.forEach((type) => {
        this.results.set(type, { items: [], total: 0 })
      })
    },
    search(event) {
      if (event) {
        this.search_types = SEARCH_TYPES
        this.search_parameters.limit = PAGE_SIZE
      }
      this.search_query = this.search_query.trim()
      if (!this.search_query) {
        this.$refs.search_field.focus()
        return
      }
      this.reset()
      this.search_items().then((data) => {
        this.search_types.forEach((type) => {
          this.results.set(type, data[`${type}s`])
        })
      })
      this.searchStore.add(this.search_query)
    },
    search_items() {
      return webapi.spotify().then(({ data }) => {
        this.search_parameters.market = data.webapi_country
        const spotifyApi = new SpotifyWebApi()
        spotifyApi.setAccessToken(data.webapi_token)
        return spotifyApi.search(
          this.search_query,
          this.search_types,
          this.search_parameters
        )
      })
    },
    search_library() {
      this.$router.push({
        name: 'search-library'
      })
    },
    search_next({ loaded }) {
      const [type] = this.search_types,
        items = this.results.get(type)
      this.search_parameters.limit = PAGE_SIZE_EXPANDED
      this.search_items().then((data) => {
        const [next] = Object.values(data)
        items.items.push(...next.items)
        items.total = next.total
        if (!this.search_parameters.offset) {
          this.search_parameters.offset = 0
        }
        this.search_parameters.offset += next.limit
        loaded(next.items.length, PAGE_SIZE_EXPANDED)
      })
    },
    show(type) {
      return this.search_types.includes(type)
    },
    show_all_button(items) {
      return items.total > items.items.length
    }
  }
}
</script>
