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
                    <span class="icon is-small"><i class="mdi mdi-web" /></span>
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
                      ><i class="mdi mdi-artist"
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
                      ><i class="mdi mdi-album"
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
                      ><i class="mdi mdi-speaker"
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
                      ><i class="mdi mdi-book-open-page-variant"
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
                      ><i class="mdi mdi-spotify"
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
