<template>
  <nav class="fd-top-navbar navbar is-light is-fixed-top" :style="zindex" role="navigation" aria-label="main navigation">
    <div class="navbar-brand">
      <navbar-item-link to="/playlists" v-if="is_visible_playlists">
        <span class="icon"><i class="mdi mdi-library-music"></i></span>
      </navbar-item-link>
      <navbar-item-link to="/music" v-if="is_visible_music">
        <span class="icon"><i class="mdi mdi-music"></i></span>
      </navbar-item-link>
      <navbar-item-link to="/podcasts" v-if="is_visible_podcasts">
        <span class="icon"><i class="mdi mdi-microphone"></i></span>
      </navbar-item-link>
      <navbar-item-link to="/audiobooks" v-if="is_visible_audiobooks">
        <span class="icon"><i class="mdi mdi-book-open-variant"></i></span>
      </navbar-item-link>
      <navbar-item-link to="/radio" v-if="is_visible_radio">
        <span class="icon"><i class="mdi mdi-radio"></i></span>
      </navbar-item-link>
      <navbar-item-link to="/files" v-if="is_visible_files">
        <span class="icon"><i class="mdi mdi-folder-open"></i></span>
      </navbar-item-link>
      <navbar-item-link to="/search" v-if="is_visible_search">
        <span class="icon"><i class="mdi mdi-magnify"></i></span>
      </navbar-item-link>

      <div class="navbar-burger" @click="show_burger_menu = !show_burger_menu" :class="{ 'is-active': show_burger_menu }">
        <span></span>
        <span></span>
        <span></span>
      </div>
    </div>

    <div class="navbar-menu" :class="{ 'is-active': show_burger_menu }">
      <div class="navbar-start">
      </div>

      <div class="navbar-end">

        <!-- Burger menu entries -->
        <div class="navbar-item has-dropdown is-hoverable"
            :class="{ 'is-active': show_settings_menu }"
            @click="on_click_outside_settings">
          <a class="navbar-link is-arrowless">
            <span class="icon is-hidden-touch"><i class="mdi mdi-24px mdi-menu"></i></span>
            <span class="is-hidden-desktop has-text-weight-bold">OwnTone</span>
          </a>

          <div class="navbar-dropdown is-right">

            <navbar-item-link to="/playlists"><span class="icon"><i class="mdi mdi-library-music"></i></span> <b>Playlists</b></navbar-item-link>
            <navbar-item-link to="/music" exact><span class="icon"><i class="mdi mdi-music"></i></span> <b>Music</b></navbar-item-link>
            <navbar-item-link to="/music/artists"><span class="fd-navbar-item-level2">Artists</span></navbar-item-link>
            <navbar-item-link to="/music/albums"><span class="fd-navbar-item-level2">Albums</span></navbar-item-link>
            <navbar-item-link to="/music/genres"><span class="fd-navbar-item-level2">Genres</span></navbar-item-link>
            <navbar-item-link to="/music/spotify" v-if="spotify_enabled"><span class="fd-navbar-item-level2">Spotify</span></navbar-item-link>
            <navbar-item-link to="/podcasts"><span class="icon"><i class="mdi mdi-microphone"></i></span> <b>Podcasts</b></navbar-item-link>
            <navbar-item-link to="/audiobooks"><span class="icon"><i class="mdi mdi-book-open-variant"></i></span> <b>Audiobooks</b></navbar-item-link>
            <navbar-item-link to="/radio"><span class="icon"><i class="mdi mdi-radio"></i></span> <b>Radio</b></navbar-item-link>
            <navbar-item-link to="/files"><span class="icon"><i class="mdi mdi-folder-open"></i></span> <b>Files</b></navbar-item-link>
            <navbar-item-link to="/search"><span class="icon"><i class="mdi mdi-magnify"></i></span> <b>Search</b></navbar-item-link>
            <hr class="fd-navbar-divider">

            <navbar-item-link to="/settings/webinterface">Settings</navbar-item-link>
            <a class="navbar-item" @click.stop.prevent="show_update_dialog = true; show_settings_menu = false; show_burger_menu = false">
              Update Library
            </a>
            <navbar-item-link to="/about">About</navbar-item-link>

            <div class="navbar-item is-hidden-desktop" style="margin-bottom: 2.5rem;"></div>
          </div>
        </div>
      </div>
    </div>

    <div class="is-overlay" v-show="show_settings_menu"
        style="z-index:10; width: 100vw; height:100vh;"
        @click="show_settings_menu = false"></div>
  </nav>
</template>

<script>
import NavbarItemLink from './NavbarItemLink.vue'
import * as types from '@/store/mutation_types'

export default {
  name: 'NavbarTop',
  components: { NavbarItemLink },

  data () {
    return {
      show_settings_menu: false
    }
  },

  computed: {
    is_visible_playlists () {
      return this.$store.getters.settings_option('webinterface', 'show_menu_item_playlists').value
    },
    is_visible_music () {
      return this.$store.getters.settings_option('webinterface', 'show_menu_item_music').value
    },
    is_visible_podcasts () {
      return this.$store.getters.settings_option('webinterface', 'show_menu_item_podcasts').value
    },
    is_visible_audiobooks () {
      return this.$store.getters.settings_option('webinterface', 'show_menu_item_audiobooks').value
    },
    is_visible_radio () {
      return this.$store.getters.settings_option('webinterface', 'show_menu_item_radio').value
    },
    is_visible_files () {
      return this.$store.getters.settings_option('webinterface', 'show_menu_item_files').value
    },
    is_visible_search () {
      return this.$store.getters.settings_option('webinterface', 'show_menu_item_search').value
    },

    player () {
      return this.$store.state.player
    },

    config () {
      return this.$store.state.config
    },

    library () {
      return this.$store.state.library
    },

    audiobooks () {
      return this.$store.state.audiobooks_count
    },

    podcasts () {
      return this.$store.state.podcasts_count
    },

    spotify_enabled () {
      return this.$store.state.spotify.webapi_token_valid
    },

    show_burger_menu: {
      get () {
        return this.$store.state.show_burger_menu
      },
      set (value) {
        this.$store.commit(types.SHOW_BURGER_MENU, value)
      }
    },

    show_player_menu () {
      return this.$store.state.show_player_menu
    },

    show_update_dialog: {
      get () {
        return this.$store.state.show_update_dialog
      },
      set (value) {
        this.$store.commit(types.SHOW_UPDATE_DIALOG, value)
      }
    },

    zindex () {
      if (this.show_player_menu) {
        return 'z-index: 20'
      }
      return ''
    }
  },

  methods: {
    on_click_outside_settings () {
      this.show_settings_menu = !this.show_settings_menu
    }
  },

  watch: {
    $route (to, from) {
      this.show_settings_menu = false
    }
  }
}
</script>

<style>
</style>
