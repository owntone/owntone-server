<template>
  <section v-if="spotify_enabled">
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
import { useServicesStore } from '@/stores/services'

export default {
  name: 'TabsSearch',
  emits: ['search-library', 'search-spotify'],

  setup() {
    return { servicesStore: useServicesStore() }
  },

  computed: {
    spotify_enabled() {
      return this.servicesStore.spotify.webapi_token_valid
    }
  }
}
</script>
