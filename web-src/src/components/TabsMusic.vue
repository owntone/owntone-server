<template>
  <section class="section fd-tabs-section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="tabs is-centered is-small">
            <ul>
              <router-link
                v-slot="{ navigate, isActive }"
                to="/music/browse"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="icon is-small"
                      ><mdicon name="web" size="16"
                    /></span>
                    <span class="">Browse</span>
                  </a>
                </li>
              </router-link>
              <router-link
                v-slot="{ navigate, isActive }"
                to="/music/artists"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="icon is-small"
                      ><mdicon name="account-music" size="16"
                    /></span>
                    <span class="">Artists</span>
                  </a>
                </li>
              </router-link>
              <router-link
                v-slot="{ navigate, isActive }"
                to="/music/albums"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="icon is-small"
                      ><mdicon name="album" size="16"
                    /></span>
                    <span class="">Albums</span>
                  </a>
                </li>
              </router-link>
              <router-link
                v-slot="{ navigate, isActive }"
                to="/music/genres"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="icon is-small"
                      ><mdicon name="speaker" size="16"
                    /></span>
                    <span class="">Genres</span>
                  </a>
                </li>
              </router-link>
              <router-link
                v-slot="{ navigate, isActive }"
                to="/music/composers"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="icon is-small"
                      ><mdicon name="book-open-page-variant" size="16"
                    /></span>
                    <span class="">Composers</span>
                  </a>
                </li>
              </router-link>
              <router-link
                v-if="spotify_enabled"
                v-slot="{ navigate, isActive }"
                to="/music/spotify"
                custom
              >
                <li :class="{ 'is-active': isActive }">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="icon is-small"
                      ><mdicon name="spotify" size="16"
                    /></span>
                    <span class="">Spotify</span>
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
