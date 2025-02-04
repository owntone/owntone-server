<template>
  <section class="section tabs-section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="tabs is-centered is-small">
            <ul>
              <router-link
                v-slot="{ navigate, isActive }"
                :to="{ name: 'music-history' }"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <mdicon class="icon is-small" name="history" size="16" />
                    <span
                      class="is-hidden-mobile"
                      v-text="$t('page.music.tabs.history')"
                    />
                  </a>
                </li>
              </router-link>
              <router-link
                v-slot="{ navigate, isActive }"
                :to="{ name: 'music-artists' }"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <mdicon
                      class="icon is-small"
                      name="account-music"
                      size="16"
                    />
                    <span
                      class="is-hidden-mobile"
                      v-text="$t('page.music.tabs.artists')"
                    />
                  </a>
                </li>
              </router-link>
              <router-link
                v-slot="{ navigate, isActive }"
                :to="{ name: 'music-albums' }"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <mdicon class="icon is-small" name="album" size="16" />
                    <span
                      class="is-hidden-mobile"
                      v-text="$t('page.music.tabs.albums')"
                    />
                  </a>
                </li>
              </router-link>
              <router-link
                v-slot="{ navigate, isActive }"
                :to="{ name: 'music-genres' }"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <mdicon class="icon is-small" name="speaker" size="16" />
                    <span
                      class="is-hidden-mobile"
                      v-text="$t('page.music.tabs.genres')"
                    />
                  </a>
                </li>
              </router-link>
              <router-link
                v-slot="{ navigate, isActive }"
                :to="{ name: 'music-composers' }"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <mdicon
                      class="icon is-small"
                      name="book-open-page-variant"
                      size="16"
                    />
                    <span
                      class="is-hidden-mobile"
                      v-text="$t('page.music.tabs.composers')"
                    />
                  </a>
                </li>
              </router-link>
              <router-link
                v-if="spotify_enabled"
                v-slot="{ navigate, isActive }"
                :to="{ name: 'music-spotify' }"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <mdicon class="icon is-small" name="spotify" size="16" />
                    <span
                      class="is-hidden-mobile"
                      v-text="$t('page.music.tabs.spotify')"
                    />
                  </a>
                </li>
              </router-link>
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
  name: 'TabsMusic',

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
