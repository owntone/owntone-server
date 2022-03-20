<template>
  <section v-if="spotify_enabled" class="section fd-remove-padding-bottom">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="tabs is-centered is-small is-toggle is-toggle-rounded">
            <ul>
              <li :class="{ 'is-active': $store.state.search_path === '/search/library' }">
                <a @click="search_library">
                  <span class="icon is-small"
                    ><i class="mdi mdi-library-books"
                  /></span>
                  <span class="">Library</span>
                </a>
              </li>
              <li :class="{ 'is-active': $store.state.search_path === '/search/spotify' }">
                <a @click="search_spotify">
                  <span class="icon is-small"
                    ><i class="mdi mdi-spotify"
                  /></span>
                  <span class="">Spotify</span>
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

    route_query: function () {
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
    search_library: function () {
      this.$store.commit(types.SEARCH_PATH, '/search/library')
      this.$router.push({
        path: this.$store.state.search_path,
        query: this.route_query
      })
    },

    search_spotify: function () {
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
