<template>
  <content-with-search
    :components="components"
    :expanded="expanded"
    :get-items="getItems"
    :history="history"
    :load="(expanded && searchNext) || null"
    :results="results"
    @search="search"
    @search-library="searchLibrary"
    @search-query="openSearch"
    @search-spotify="search"
    @expand="expand"
  />
</template>

<script>
import ContentWithSearch from '@/templates/ContentWithSearch.vue'
import ListAlbumsSpotify from '@/components/ListAlbumsSpotify.vue'
import ListArtistsSpotify from '@/components/ListArtistsSpotify.vue'
import ListPlaylistsSpotify from '@/components/ListPlaylistsSpotify.vue'
import ListTracksSpotify from '@/components/ListTracksSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import services from '@/api/services'
import { useSearchStore } from '@/stores/search'

const PAGE_SIZE = 3
const PAGE_SIZE_EXPANDED = 50
const SEARCH_TYPES = ['track', 'artist', 'album', 'playlist']

export default {
  name: 'PageSearchSpotify',
  components: { ContentWithSearch },
  setup() {
    return {
      components: {
        album: ListAlbumsSpotify,
        artist: ListArtistsSpotify,
        playlist: ListPlaylistsSpotify,
        track: ListTracksSpotify
      },
      searchStore: useSearchStore()
    }
  },
  data() {
    return {
      parameters: {},
      results: new Map(),
      types: SEARCH_TYPES
    }
  },
  computed: {
    expanded() {
      return this.types.length === 1
    },
    history() {
      return this.searchStore.history.filter(
        (query) => !query.startsWith('query:')
      )
    }
  },
  mounted() {
    this.search()
  },
  methods: {
    expand(type) {
      this.search([type], PAGE_SIZE_EXPANDED)
    },
    getItems(items) {
      return items.items
    },
    openSearch(query) {
      this.searchStore.query = query
      this.search()
    },
    reset() {
      this.results.clear()
      this.types.forEach((type) => {
        this.results.set(type, { items: [], total: 0 })
      })
    },
    search(types = SEARCH_TYPES, limit = PAGE_SIZE, offset = 0) {
      if (this.searchStore.query) {
        this.types = types
        this.parameters.limit = limit
        this.parameters.offset = offset
        this.searchStore.query = this.searchStore.query.trim()
        this.reset()
        this.searchItems().then((data) => {
          this.types.forEach((type) => {
            this.results.set(type, data[`${type}s`])
          })
        })
        this.searchStore.add(this.searchStore.query)
      }
    },
    searchItems() {
      return services.spotify().then((data) => {
        this.parameters.market = data.webapi_country
        const spotifyApi = new SpotifyWebApi()
        spotifyApi.setAccessToken(data.webapi_token)
        return spotifyApi.search(
          this.searchStore.query,
          this.types,
          this.parameters
        )
      })
    },
    searchLibrary() {
      this.$router.push({ name: 'search-library' })
    },
    searchNext({ loaded }) {
      const items = this.results.get(this.types[0])
      this.parameters.limit = PAGE_SIZE_EXPANDED
      this.searchItems().then((data) => {
        const [next] = Object.values(data)
        items.items.push(...next.items)
        this.parameters.offset += next.items.length
        loaded(Number(next.next && next.limit), PAGE_SIZE_EXPANDED)
      })
    }
  }
}
</script>
