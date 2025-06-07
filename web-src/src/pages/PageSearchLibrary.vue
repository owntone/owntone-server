<template>
  <content-with-search
    :components="components"
    :expanded="expanded"
    :get-items="getItems"
    :history="history"
    :results="results"
    @search="search"
    @search-library="search"
    @search-query="openSearch"
    @search-spotify="searchSpotify"
    @expand="expand"
  >
    <template #help>
      <i18n-t
        tag="p"
        class="help has-text-centered"
        keypath="page.search.help"
        scope="global"
      >
        <template #query>
          <code>query:</code>
        </template>
        <template #help>
          <a
            href="https://owntone.github.io/owntone-server/smart-playlists/"
            target="_blank"
            v-text="$t('page.search.expression')"
          />
        </template>
      </i18n-t>
    </template>
  </content-with-search>
</template>

<script>
import ContentWithSearch from '@/templates/ContentWithSearch.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import ListArtists from '@/components/ListArtists.vue'
import ListComposers from '@/components/ListComposers.vue'
import ListPlaylists from '@/components/ListPlaylists.vue'
import ListTracks from '@/components/ListTracks.vue'
import library from '@/api/library'
import { useSearchStore } from '@/stores/search'

const PAGE_SIZE = 3
const SEARCH_TYPES = [
  'track',
  'artist',
  'album',
  'composer',
  'playlist',
  'audiobook',
  'podcast'
]

export default {
  name: 'PageSearchLibrary',
  components: { ContentWithSearch },
  setup() {
    return {
      components: {
        album: ListAlbums,
        audiobook: ListAlbums,
        artist: ListArtists,
        composer: ListComposers,
        playlist: ListPlaylists,
        podcast: ListAlbums,
        track: ListTracks
      },
      searchStore: useSearchStore()
    }
  },
  data() {
    return {
      limit: PAGE_SIZE,
      results: new Map(),
      types: SEARCH_TYPES
    }
  },
  computed: {
    expanded() {
      return this.types.length === 1
    },
    history() {
      return this.searchStore.history
    }
  },
  mounted() {
    this.search()
  },
  methods: {
    expand(type) {
      this.search([type], -1)
    },
    getItems(items) {
      return items
    },
    openSearch(query) {
      this.searchStore.query = query
      this.search()
    },
    reset() {
      this.results.clear()
      this.types.forEach((type) => {
        this.results.set(type, new GroupedList())
      })
    },
    search(types = SEARCH_TYPES, limit = PAGE_SIZE) {
      if (this.searchStore.query) {
        this.types = types
        this.limit = limit
        this.searchStore.query = this.searchStore.query.trim()
        this.reset()
        this.types.forEach((type) => {
          this.searchItems(type)
        })
        this.searchStore.add(this.searchStore.query)
      }
    },
    searchItems(type) {
      const music = type !== 'audiobook' && type !== 'podcast'
      const kind = (music && 'music') || type
      const parameters = {
        limit: this.limit,
        type: (music && type) || 'album'
      }
      if (this.searchStore.query.startsWith('query:')) {
        parameters.expression = `(${this.searchStore.query.replace(/^query:/u, '').trim()}) and media_kind is ${kind}`
      } else if (music) {
        parameters.query = this.searchStore.query
        parameters.media_kind = kind
      } else {
        parameters.expression = `(album includes "${this.searchStore.query}" or artist includes "${this.searchStore.query}") and media_kind is ${kind}`
      }
      library.search(parameters).then((data) => {
        this.results.set(type, new GroupedList(data[`${parameters.type}s`]))
      })
    },
    searchSpotify() {
      this.$router.push({ name: 'search-spotify' })
    }
  }
}
</script>
