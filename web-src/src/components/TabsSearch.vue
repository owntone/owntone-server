<template>
  <section v-if="spotify_enabled" class="section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="tabs is-centered is-small is-toggle is-toggle-rounded">
            <ul>
              <li
                :class="{
                  'is-active': $store.state.search_path === '/search/library'
                }"
              >
                <a @click="search_library">
                  <mdicon class="icon is-small" name="bookshelf" size="16" />
                  <span v-text="$t('page.search.tabs.library')" />
                </a>
              </li>
              <li
                :class="{
                  'is-active': $store.state.search_path === '/search/spotify'
                }"
              >
                <a @click="search_spotify">
                  <mdicon class="icon is-small" name="spotify" size="16" />
                  <span v-text="$t('page.search.tabs.spotify')" />
                </a>
              </li>
            </ul>
          </div>
        </div>
      </div>
    </div>
  </section>
</template>

<script>
import * as types from '@/store/mutation_types'

export default {
  name: 'TabsSearch',

  props: ['query'],

  computed: {
    spotify_enabled() {
      return this.$store.state.spotify.webapi_token_valid
    },

    route_query() {
      if (!this.query) {
        return null
      }

      return {
        type: 'track,artist,album,playlist,audiobook,podcast',
        query: this.query,
        limit: 3,
        offset: 0
      }
    }
  },

  methods: {
    search_library() {
      this.$store.commit(types.SEARCH_PATH, '/search/library')
      this.$router.push({
        path: this.$store.state.search_path,
        query: this.route_query
      })
    },

    search_spotify() {
      this.$store.commit(types.SEARCH_PATH, '/search/spotify')
      this.$router.push({
        path: this.$store.state.search_path,
        query: this.route_query
      })
    }
  }
}
</script>

<style></style>
