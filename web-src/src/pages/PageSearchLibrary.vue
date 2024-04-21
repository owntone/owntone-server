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
            <div v-for="query in recent_searches" :key="query" class="control">
              <div class="tags has-addons">
                <a class="tag" @click="open_search(query)" v-text="query" />
                <a class="tag is-delete" @click="remove_search(query)"></a>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </section>
  <tabs-search @search-library="search" @search-spotify="search_spotify" />
  <template v-for="[type, items] in results" :key="type">
    <content-with-heading class="pt-0">
      <template #heading-left>
        <p class="title is-4" v-text="$t(`page.search.${type}s`)" />
      </template>
      <template #content>
        <component :is="components[type]" :items="items" />
      </template>
      <template v-if="!expanded" #footer>
        <nav v-if="show_all_button(items)" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="expand(type)"
              v-text="
                $t(`page.search.show-${type}s`, items.total, {
                  count: $filters.number(items.total)
                })
              "
            />
          </p>
        </nav>
        <p v-if="!items.total" class="has-text-centered-mobile">
          <i v-text="$t('page.search.no-results')" />
        </p>
      </template>
    </content-with-heading>
  </template>
</template>

<script>
import * as types from '@/store/mutation_types'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import ListArtists from '@/components/ListArtists.vue'
import ListComposers from '@/components/ListComposers.vue'
import ListPlaylists from '@/components/ListPlaylists.vue'
import ListTracks from '@/components/ListTracks.vue'
import TabsSearch from '@/components/TabsSearch.vue'
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
    ListAlbums,
    ListArtists,
    ListComposers,
    ListPlaylists,
    ListTracks,
    TabsSearch
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
      search_limit: {},
      search_query: '',
      search_types: SEARCH_TYPES
    }
  },

  computed: {
    expanded() {
      return this.search_types.length === 1
    },
    recent_searches() {
      return this.$store.state.recent_searches
    }
  },

  watch: {
    search_query() {
      this.$store.commit(types.SEARCH_QUERY, this.search_query)
    }
  },

  mounted() {
    this.$store.commit(types.SEARCH_SOURCE, this.$route.name)
    this.search_query = this.$store.state.search_query
    this.search_limit = PAGE_SIZE
    this.search()
  },

  methods: {
    expand(type) {
      this.search_query = this.$store.state.search_query
      this.search_types = [type]
      this.search_limit = -1
      this.search()
    },
    open_search(query) {
      this.search_query = query
      this.search_types = SEARCH_TYPES
      this.search_limit = PAGE_SIZE
      this.search()
    },
    remove_search(query) {
      this.$store.dispatch('remove_recent_search', query)
    },
    reset() {
      this.results.clear()
      this.search_types.forEach((type) => {
        this.results.set(type, new GroupedList())
      })
    },
    search(event) {
      if (event) {
        this.search_types = SEARCH_TYPES
        this.search_limit = PAGE_SIZE
      }
      this.search_query = this.search_query.trim()
      if (!this.search_query || !this.search_query.replace(/^query:/u, '')) {
        this.$refs.search_field.focus()
        return
      }
      this.reset()
      this.search_types.forEach((type) => {
        this.search_items(type)
      })
      this.$store.dispatch('add_recent_search', this.search_query)
    },
    search_items(type) {
      const music = type !== 'audiobook' && type !== 'podcast',
        kind = music ? 'music' : type,
        parameters = {
          type: music ? type : 'album',
          limit: this.search_limit
        }
      if (this.search_query.startsWith('query:')) {
        parameters.expression = `(${this.search_query.replace(/^query:/u, '').trim()}) and media_kind is ${kind}`
      } else if (music) {
        parameters.query = this.search_query
        parameters.media_kind = kind
      } else {
        parameters.expression = `(album includes "${this.search_query}" or artist includes "${this.search_query}") and media_kind is ${kind}`
      }
      webapi.search(parameters).then(({ data }) => {
        this.results.set(type, new GroupedList(data[`${parameters.type}s`]))
      })
    },
    search_spotify() {
      this.$router.push({ name: 'search-spotify' })
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

<style></style>
