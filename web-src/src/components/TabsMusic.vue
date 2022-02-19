<template>
  <section class="section fd-tabs-section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="tabs is-centered is-small">
            <ul>
              <router-link to="/music/browse" custom v-slot="{ navigate, isActive }">
                <li :class="{'is-active': isActive}">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="icon is-small"><i class="mdi mdi-web"></i></span>
                    <span class="">Browse</span>
                  </a>
                </li>
              </router-link>
              <router-link to="/music/artists" custom v-slot="{ navigate, isActive }">
                <li :class="{'is-active': isActive}">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="icon is-small"><i class="mdi mdi-artist"></i></span>
                    <span class="">Artists</span>
                  </a>
                </li>
              </router-link>
              <router-link to="/music/albums" custom v-slot="{ navigate, isActive }">
                <li :class="{'is-active': isActive}">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="icon is-small"><i class="mdi mdi-album"></i></span>
                    <span class="">Albums</span>
                  </a>
                </li>
              </router-link>
              <router-link to="/music/genres" custom v-slot="{ navigate, isActive }">
                <li :class="{'is-active': isActive}">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="icon is-small"><i class="mdi mdi-speaker"></i></span>
                    <span class="">Genres</span>
                  </a>
                </li>
              </router-link>
              <router-link to="/music/composers" custom v-slot="{ navigate, isActive }">
                <li :class="{'is-active': isActive}">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="icon is-small"><i class="mdi mdi-book-open-page-variant"></i></span>
                    <span class="">Composers</span>
                  </a>
                </li>
              </router-link>
              <router-link to="/music/spotify" v-if="spotify_enabled" custom v-slot="{ navigate, isActive }">
                <li :class="{'is-active': isActive}">
                  <a @click="navigate" @keypress.enter="navigate">
                    <span class="icon is-small"><i class="mdi mdi-spotify"></i></span>
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
    spotify_enabled () {
      return this.$store.state.spotify.webapi_token_valid
    }
  }
}
</script>

<style>
</style>
