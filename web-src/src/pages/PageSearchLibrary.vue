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
  <template v-for="type in search_types" :key="type">
    <content-with-heading v-if="show(type)" class="pt-0">
      <template #heading-left>
        <p class="title is-4" v-text="$t(`page.search.${type}s`)" />
      </template>
      <template #content>
        <component :is="components[type]" :items="results[type]" />
      </template>
      <template #footer>
        <nav v-if="show_all_button(type)" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search(type)"
              v-text="
                $t(`page.search.show-${type}s`, results[type].total, {
                  count: $filters.number(results[type].total)
                })
              "
            />
          </p>
        </nav>
        <p v-if="!results[type].total" class="has-text-centered-mobile">
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
      results: {
        album: new GroupedList(),
        artist: new GroupedList(),
        audiobook: new GroupedList(),
        composer: new GroupedList(),
        playlist: new GroupedList(),
        podcast: new GroupedList(),
        track: new GroupedList()
      },
      search_query: '',
      search_types: [
        'track',
        'artist',
        'album',
        'composer',
        'playlist',
        'audiobook',
        'podcast'
      ],
      tracks: new GroupedList()
    }
  },

  computed: {
    recent_searches() {
      return this.$store.state.recent_searches
    }
  },

  watch: {
    $route(to, from) {
      this.search(to)
    }
  },

  mounted() {
    this.$store.commit(types.SEARCH_SOURCE, this.$route.name)
    this.search(this.$route)
  },

  methods: {
    new_search() {
      if (!this.search_query) {
        return
      }
      this.$router.push({
        query: {
          limit: 3,
          offset: 0,
          query: this.search_query,
          type: this.search_types.join()
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
        query: { query: this.$route.query.query, type }
      })
    },
    search(route) {
      this.search_query = route.query.query?.trim()
      if (!this.search_query || !this.search_query.replace(/^query:/u, '')) {
        this.$refs.search_field.focus()
        return
      }
      route.query.query = this.search_query
      this.searchMusic(route.query)
      this.searchType(route.query, 'audiobook')
      this.searchType(route.query, 'podcast')
      this.$store.dispatch('add_recent_search', this.search_query)
    },
    searchMusic(query) {
      if (
        !query.type.includes('track') &&
        !query.type.includes('artist') &&
        !query.type.includes('album') &&
        !query.type.includes('playlist') &&
        !query.type.includes('composer')
      ) {
        return
      }
      const parameters = {
        type: query.type
      }
      if (query.query.startsWith('query:')) {
        parameters.expression = `(${query.query.replace(/^query:/u, '').trim()}) and media_kind is music`
      } else {
        parameters.query = query.query
        parameters.media_kind = 'music'
      }
      if (query.limit) {
        parameters.limit = query.limit
        parameters.offset = query.offset
      }
      webapi.search(parameters).then(({ data }) => {
        this.results.track = new GroupedList(data.tracks)
        this.results.artist = new GroupedList(data.artists)
        this.results.album = new GroupedList(data.albums)
        this.results.composer = new GroupedList(data.composers)
        this.results.playlist = new GroupedList(data.playlists)
      })
    },
    searchType(query, type) {
      if (!query.type.includes(type)) {
        return
      }
      const parameters = {
        type: 'album'
      }
      if (query.query.startsWith('query:')) {
        parameters.expression = query.query.replace(/^query:/u, '').trim()
      } else {
        parameters.expression = `album includes "${query.query}" or artist includes "${query.query}"`
      }
      parameters.expression = `(${parameters.expression}) and media_kind is ${type}`
      if (query.limit) {
        parameters.limit = query.limit
        parameters.offset = query.offset
      }
      webapi.search(parameters).then(({ data }) => {
        this.results[type] = new GroupedList(data.albums)
      })
    },
    show(type) {
      return this.$route.query.type?.includes(type) ?? false
    },
    show_all_button(type) {
      const items = this.results[type]
      return items.total > items.items.length
    }
  }
}
</script>

<style></style>
