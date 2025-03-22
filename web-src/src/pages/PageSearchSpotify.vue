<template>
  <section class="section pb-0">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <form @submit.prevent="search">
            <div class="field">
              <div class="control has-icons-left">
                <input
                  v-model="searchStore.query"
                  class="input is-rounded"
                  type="text"
                  :placeholder="$t('page.search.placeholder')"
                  autocomplete="off"
                />
                <mdicon class="icon is-left" name="magnify" size="16" />
              </div>
            </div>
          </form>
          <div class="field is-grouped is-grouped-multiline mt-4">
            <div v-for="item in history" :key="item" class="control">
              <div class="tags has-addons">
                <a class="tag" @click="openSearch(item)" v-text="item" />
                <a class="tag is-delete" @click="searchStore.remove(item)" />
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </section>
  <tabs-search @search-library="searchLibrary" @search-spotify="search" />
  <content-with-heading v-for="[type, items] in results" :key="type">
    <template #heading>
      <heading-title :content="{ title: $t(`page.search.${type}s`) }" />
    </template>
    <template #content>
      <component :is="components[type]" :items="items.items" />
      <vue-eternal-loading v-if="expanded" :load="searchNext">
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
      <control-button
        v-if="showAllButton(items)"
        :button="{
          handler: () => expand(type),
          title: $t(
            `page.search.show-${type}s`,
            { count: $n(items.total) },
            items.total
          )
        }"
      />
      <div v-if="!items.total" class="has-text-centered-mobile">
        <i v-text="$t('page.search.no-results')" />
      </div>
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import HeadingTitle from '@/components/HeadingTitle.vue'
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
    ControlButton,
    ContentWithHeading,
    HeadingTitle,
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
      parameters: {},
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
    this.searchStore.source = this.$route.name
    this.parameters.limit = PAGE_SIZE
    this.search()
  },
  methods: {
    expand(type) {
      this.types = [type]
      this.parameters.limit = PAGE_SIZE_EXPANDED
      this.parameters.offset = 0
      this.search()
    },
    openSearch(query) {
      this.searchStore.query = query
      this.types = SEARCH_TYPES
      this.parameters.limit = PAGE_SIZE
      this.parameters.offset = 0
      this.search()
    },
    reset() {
      this.results.clear()
      this.types.forEach((type) => {
        this.results.set(type, { items: [], total: 0 })
      })
    },
    search(event) {
      if (this.searchStore.query) {
        if (event) {
          this.types = SEARCH_TYPES
          this.parameters.limit = PAGE_SIZE
        }
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
      return webapi.spotify().then(({ data }) => {
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
      const [type] = this.types,
        items = this.results.get(type)
      this.parameters.limit = PAGE_SIZE_EXPANDED
      this.searchItems().then((data) => {
        const [next] = Object.values(data)
        items.items.push(...next.items)
        items.total = next.total
        if (!this.parameters.offset) {
          this.parameters.offset = 0
        }
        this.parameters.offset += next.limit
        loaded(next.items.length, PAGE_SIZE_EXPANDED)
      })
    },
    show(type) {
      return this.types.includes(type)
    },
    showAllButton(items) {
      return items.total > items.items.length
    }
  }
}
</script>
