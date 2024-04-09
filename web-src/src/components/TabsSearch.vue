<template>
  <section v-if="spotify_enabled" class="section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="tabs is-centered is-small is-toggle is-toggle-rounded">
            <ul>
              <li
                :class="{
                  'is-active': $route.name === 'search-library'
                }"
              >
                <a @click="$emit('search-library')">
                  <mdicon class="icon is-small" name="bookshelf" size="16" />
                  <span v-text="$t('page.search.tabs.library')" />
                </a>
              </li>
              <li
                :class="{
                  'is-active': $route.name === 'search-spotify'
                }"
              >
                <a @click="$emit('search-spotify')">
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
export default {
  name: 'TabsSearch',
  emits: ['search-library', 'search-spotify'],

  computed: {
    spotify_enabled() {
      return this.$store.state.spotify.webapi_token_valid
    }
  }
}
</script>

<style></style>
