<template>
  <section class="section fd-tabs-section">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <div class="tabs is-centered is-small">
            <ul>
              <router-link tag="li" to="/music/browse" active-class="is-active">
                <a>
                  <span class="icon is-small"><i class="mdi mdi-web"></i></span>
                  <span class="">Browse</span>
                </a>
              </router-link>
              <router-link tag="li" to="/music/artists" active-class="is-active">
                <a>
                  <span class="icon is-small"><i class="mdi mdi-artist"></i></span>
                  <span class="">Artists</span>
                </a>
              </router-link>
              <router-link tag="li" to="/music/albums" active-class="is-active">
                <a>
                  <span class="icon is-small"><i class="mdi mdi-album"></i></span>
                  <span class="">Albums</span>
                </a>
              </router-link>
              <router-link tag="li" to="/music/spotify" v-if="spotify_enabled" active-class="is-active">
                <a>
                  <span class="icon is-small"><i class="mdi mdi-spotify"></i></span>
                  <span class="">Spotify</span>
                </a>
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
