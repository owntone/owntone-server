<template>
  <nav class="navbar is-dark is-fixed-bottom" role="navigation" aria-label="player controls">
    <div class="navbar-brand fd-expanded">
      <router-link to="/" class="navbar-item" active-class="is-active" exact>
        <span class="icon"><i class="mdi mdi-24px mdi-playlist-play"></i></span>
      </router-link>
      <router-link to="/now-playing" class="navbar-item is-expanded is-clipped" active-class="is-active" exact>
        <div>
          <p class="is-size-7 fd-is-text-clipped">
            <strong>{{ now_playing.title }}</strong><br>
            {{ now_playing.artist }}
          </p>
        </div>
      </router-link>
      <player-button-play-pause class="navbar-item fd-margin-left-auto" icon_style="mdi-36px"></player-button-play-pause>
    </div>
  </nav>
</template>

<script>
import PlayerButtonPlayPause from './PlayerButtonPlayPause'

export default {
  name: 'NavbarBottom',
  components: { PlayerButtonPlayPause },

  data () {
    return { }
  },

  computed: {
    state () {
      return this.$store.state.player
    },
    now_playing () {
      return this.$store.getters.now_playing
    }
  }
}
</script>

<style>
</style>
