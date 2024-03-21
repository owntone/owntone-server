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
    <content-with-heading v-if="show('track') && tracks.total" class="pt-0">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.search.tracks')" />
      </template>
      <template #content>
        <list-tracks :tracks="tracks" />
      </template>
      <template #footer>
        <nav v-if="show_all_button(tracks)" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search('track')"
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
    <content-text v-if="show('track') && !tracks.total" class="pt-0">
      <template #content>
        <p><i v-text="$t('page.search.no-tracks')" /></p>
      </template>
    </content-text>
    <!-- Artists -->
    <content-with-heading v-if="show('artist') && artists.total">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.search.artists')" />
      </template>
      <template #content>
        <list-artists :artists="artists" />
      </template>
      <template #footer>
        <nav v-if="show_all_button(artists)" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search('artist')"
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
    <content-text v-if="show('artist') && !artists.total">
      <template #content>
        <p><i v-text="$t('page.search.no-artists')" /></p>
      </template>
    </content-text>
    <!-- Albums -->
    <content-with-heading v-if="show('album') && albums.total">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.search.albums')" />
      </template>
      <template #content>
        <list-albums :albums="albums" />
      </template>
      <template #footer>
        <nav v-if="show_all_button(albums)" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search('album')"
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
    <content-text v-if="show('album') && !albums.total">
      <template #content>
        <p><i v-text="$t('page.search.no-albums')" /></p>
      </template>
    </content-text>
    <!-- Composers -->
    <content-with-heading v-if="show('composer') && composers.total">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.search.composers')" />
      </template>
      <template #content>
        <list-composers :composers="composers" />
      </template>
      <template #footer>
        <nav v-if="show_all_button(composers)" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search('composer')"
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
    <content-text v-if="show('composer') && !composers.total">
      <template #content>
        <p><i v-text="$t('page.search.no-composers')" /></p>
      </template>
    </content-text>
    <!-- Playlists -->
    <content-with-heading v-if="show('playlist') && playlists.total">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.search.playlists')" />
      </template>
      <template #content>
        <list-playlists :playlists="playlists" />
      </template>
      <template #footer>
        <nav v-if="show_all_button(playlists)" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search('playlist')"
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
    <content-text v-if="show('playlist') && !playlists.total">
      <template #content>
        <p><i v-text="$t('page.search.no-playlists')" /></p>
      </template>
    </content-text>
    <!-- Podcasts -->
    <content-with-heading v-if="show('podcast') && podcasts.total">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.search.podcasts')" />
      </template>
      <template #content>
        <list-albums :albums="podcasts" />
      </template>
      <template #footer>
        <nav v-if="show_all_button(podcasts)" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search('podcast')"
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
    <content-text v-if="show('podcast') && !podcasts.total">
      <template #content>
        <p><i v-text="$t('page.search.no-podcasts')" /></p>
      </template>
    </content-text>

    <!-- Audiobooks -->
    <content-with-heading v-if="show('audiobook') && audiobooks.total">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.search.audiobooks')" />
      </template>
      <template #content>
        <list-albums :albums="audiobooks" />
      </template>
      <template #footer>
        <nav v-if="show_all_button(audiobooks)" class="level">
          <p class="level-item">
            <a
              class="button is-light is-small is-rounded"
              @click="open_search('audiobook')"
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
    <content-text v-if="show('audiobook') && !audiobooks.total">
      <template #content>
        <p><i v-text="$t('page.search.no-audiobooks')" /></p>
      </template>
    </content-text>
  </div>
</template>

<script>
import ContentText from '@/templates/ContentText.vue'
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
      albums: new GroupedList(),
      artists: new GroupedList(),
      audiobooks: new GroupedList(),
      composers: new GroupedList(),
      playlists: new GroupedList(),
      podcasts: new GroupedList(),
      search_query: '',
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
    this.search(this.$route)
  },

  methods: {
    search(route) {
      this.search_query = route.query.query?.trim()
      if (!this.search_query || !this.search_query.replace(/^query:/u, '')) {
        this.$refs.search_field.focus()
        return
      }
      route.query.query = this.search_query
      this.searchMusic(route.query)
      this.searchAudiobooks(route.query)
      this.searchPodcasts(route.query)
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
      const searchParams = {
        type: query.type
      }
      if (query.query.startsWith('query:')) {
        searchParams.expression = `(${query.query.replace(/^query:/u, '').trim()}) and media_kind is music`
      } else {
        searchParams.query = query.query
        searchParams.media_kind = 'music'
      }
      if (query.limit) {
        searchParams.limit = query.limit
        searchParams.offset = query.offset
      }
      webapi.search(searchParams).then(({ data }) => {
        this.tracks = new GroupedList(data.tracks)
        this.artists = new GroupedList(data.artists)
        this.albums = new GroupedList(data.albums)
        this.composers = new GroupedList(data.composers)
        this.playlists = new GroupedList(data.playlists)
      })
    },

    searchAudiobooks(query) {
      if (!query.type.includes('audiobook')) {
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
      parameters.expression = `(${parameters.expression}) and media_kind is audiobook`
      if (query.limit) {
        parameters.limit = query.limit
        parameters.offset = query.offset
      }
      webapi.search(parameters).then(({ data }) => {
        this.audiobooks = new GroupedList(data.albums)
      })
    },

    searchPodcasts(query) {
      if (!query.type.includes('podcast')) {
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
      parameters.expression = `(${parameters.expression}) and media_kind is podcast`
      if (query.limit) {
        parameters.limit = query.limit
        parameters.offset = query.offset
      }
      webapi.search(parameters).then(({ data }) => {
        this.podcasts = new GroupedList(data.albums)
      })
    },

    new_search() {
      if (!this.search_query) {
        return
      }

      this.$router.push({
        name: 'search-library',
        query: {
          limit: 3,
          offset: 0,
          query: this.search_query,
          type: 'track,artist,album,playlist,audiobook,podcast,composer'
        }
      })
      this.$refs.search_field.blur()
    },

    show(type) {
      return this.$route.query.type?.includes(type) ?? false
    },

    show_all_button(items) {
      return items.total > items.items.length
    },

    open_search(type) {
      this.$router.push({
        name: 'search-library',
        query: {
          query: this.$route.query.query,
          type: type
        }
      })
    },

    open_recent_search(query) {
      this.search_query = query
      this.new_search()
    }
  }
}
</script>

<style></style>
