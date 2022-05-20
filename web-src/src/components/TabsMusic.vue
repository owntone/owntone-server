<template>
  <section class="section fd-tabs-section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="tabs is-centered is-small">
            <ul>
              <router-link v-slot="{ navigate, isActive }" to="/music/browse" custom>
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <mdicon class="icon is-small" name="web" size="16" />
                    <span v-text="$t('page.settings.tabs.music.browse')" />
                  </a>
                </li>
              </router-link>
              <router-link v-slot="{ navigate, isActive }" to="/music/artists" custom>
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <mdicon class="icon is-small" name="account-music" size="16" />
                    <span v-text="$t('page.settings.tabs.music.artists')" />
                  </a>
                </li>
              </router-link>
              <router-link v-slot="{ navigate, isActive }" to="/music/albums" custom>
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <mdicon class="icon is-small" name="album" size="16" />
                    <span v-text="$t('page.settings.tabs.music.albums')" />
                  </a>
                </li>
              </router-link>
              <router-link v-slot="{ navigate, isActive }" to="/music/genres" custom>
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <mdicon class="icon is-small" name="speaker" size="16" />
                    <span v-text="$t('page.settings.tabs.music.genres')" />
                  </a>
                </li>
              </router-link>
              <router-link v-slot="{ navigate, isActive }" to="/music/composers" custom>
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <mdicon class="icon is-small" name="book-open-page-variant" size="16" />
                    <span v-text="$t('page.settings.tabs.music.composers')" />
                  </a>
                </li>
              </router-link>
              <router-link v-if="spotify_enabled" v-slot="{ navigate, isActive }" to="/music/spotify" custom>
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <mdicon class="icon is-small" name="spotify" size="16" />
                    <span v-text="$t('page.settings.tabs.music.spotify')" />
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
export default {
  name: 'TabsMusic',

  computed: {
    spotify_enabled() {
      return this.$store.state.spotify.webapi_token_valid
    }
  }
}
</script>

<style></style>
