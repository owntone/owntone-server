<template>
  <section class="section pb-0">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <form @submit.prevent="search">
            <div class="field">
              <div class="control has-icons-left">
                <input
                  ref="search_field"
                  v-model="query"
                  class="input is-rounded"
                  type="text"
                  :placeholder="$t('page.search.placeholder')"
                  autocomplete="off"
                />
                <mdicon class="icon is-left" name="magnify" size="16" />
              </div>
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
            </div>
          </form>
          <div class="field is-grouped is-grouped-multiline mt-4">
            <div v-for="item in history" :key="item" class="control">
              <div class="tags has-addons">
                <a class="tag" @click="openSearch(item)" v-text="item" />
                <a class="tag is-delete" @click="removeSearch(item)" />
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </section>
  <tabs-search @search-library="search" @search-spotify="searchSpotify" />
  <template v-for="[type, items] in results" :key="type">
    <content-with-heading>
      <template #heading-left>
        <heading-title :content="{ title: $t(`page.search.${type}s`) }" />
      </template>
      <template #content>
        <component :is="components[type]" :items="items" />
      </template>
      <template v-if="!expanded" #footer>
        <nav v-if="showAllButton(items)" class="level">
          <div class="level-item">
            <control-button
              :button="{
                handler: () => expand(type),
                title: $t(
                  `page.search.show-${type}s`,
                  { count: $n(items.total) },
                  items.total
                )
              }"
            />
          </div>
        </nav>
        <div v-if="!items.total" class="has-text-centered-mobile">
          <i v-text="$t('page.search.no-results')" />
        </div>
      </template>
    </content-with-heading>
  </template>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import ListArtists from '@/components/ListArtists.vue'
import ListComposers from '@/components/ListComposers.vue'
import ListPlaylists from '@/components/ListPlaylists.vue'
import ListTracks from '@/components/ListTracks.vue'
import TabsSearch from '@/components/TabsSearch.vue'
import { useSearchStore } from '@/stores/search'
import webapi from '@/webapi'

const PAGE_SIZE = 3,
  SEARCH_TYPES = [
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
  components: {
    ContentWithHeading,
    ControlButton,
    HeadingTitle,
    ListAlbums,
    ListArtists,
    ListComposers,
    ListPlaylists,
    ListTracks,
    TabsSearch
  },
  setup() {
    return { searchStore: useSearchStore() }
  },
  data() {
    return {
      components: {
        album: ListAlbums.name,
        artist: ListArtists.name,
        audiobook: ListAlbums.name,
        composer: ListComposers.name,
        playlist: ListPlaylists.name,
        podcast: ListAlbums.name,
        track: ListTracks.name
      },
      results: new Map(),
      limit: {},
      query: '',
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
  watch: {
    query() {
      this.searchStore.query = this.query
    }
  },
  mounted() {
    this.searchStore.source = this.$route.name
    this.query = this.searchStore.query
    this.limit = PAGE_SIZE
    this.search()
  },
  methods: {
    expand(type) {
      this.query = this.searchStore.query
      this.types = [type]
      this.limit = -1
      this.search()
    },
    openSearch(query) {
      this.query = query
      this.types = SEARCH_TYPES
      this.limit = PAGE_SIZE
      this.search()
    },
    removeSearch(query) {
      this.searchStore.remove(query)
    },
    reset() {
      this.results.clear()
      this.types.forEach((type) => {
        this.results.set(type, new GroupedList())
      })
    },
    search(event) {
      if (event) {
        this.types = SEARCH_TYPES
        this.limit = PAGE_SIZE
      }
      this.query = this.query.trim()
      if (!this.query || !this.query.replace(/^query:/u, '')) {
        this.$refs.search_field.focus()
        return
      }
      this.reset()
      this.types.forEach((type) => {
        this.searchItems(type)
      })
      this.searchStore.add(this.query)
    },
    searchItems(type) {
      const music = type !== 'audiobook' && type !== 'podcast'
      const kind = music ? 'music' : type
      const parameters = {
        limit: this.limit,
        type: music ? type : 'album'
      }
      if (this.query.startsWith('query:')) {
        parameters.expression = `(${this.query.replace(/^query:/u, '').trim()}) and media_kind is ${kind}`
      } else if (music) {
        parameters.query = this.query
        parameters.media_kind = kind
      } else {
        parameters.expression = `(album includes "${this.query}" or artist includes "${this.query}") and media_kind is ${kind}`
      }
      webapi.search(parameters).then(({ data }) => {
        this.results.set(type, new GroupedList(data[`${parameters.type}s`]))
      })
    },
    searchSpotify() {
      this.$router.push({ name: 'search-spotify' })
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
