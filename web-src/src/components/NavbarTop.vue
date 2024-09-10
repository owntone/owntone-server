<template>
  <nav
    class="navbar is-light is-fixed-top"
    :style="zindex"
    role="navigation"
    aria-label="main navigation"
  >
    <div class="navbar-brand">
      <control-link
        v-if="settingsStore.show_menu_item_playlists"
        class="navbar-item"
        :to="{ name: 'playlists' }"
      >
        <mdicon class="icon" name="music-box-multiple" size="16" />
      </control-link>
      <control-link
        v-if="settingsStore.show_menu_item_music"
        class="navbar-item"
        :to="{ name: 'music' }"
      >
        <mdicon class="icon" name="music" size="16" />
      </control-link>
      <control-link
        v-if="settingsStore.show_menu_item_podcasts"
        class="navbar-item"
        :to="{ name: 'podcasts' }"
      >
        <mdicon class="icon" name="microphone" size="16" />
      </control-link>
      <control-link
        v-if="settingsStore.show_menu_item_audiobooks"
        class="navbar-item"
        :to="{ name: 'audiobooks' }"
      >
        <mdicon class="icon" name="book-open-variant" size="16" />
      </control-link>
      <control-link
        v-if="settingsStore.show_menu_item_radio"
        class="navbar-item"
        :to="{ name: 'radio' }"
      >
        <mdicon class="icon" name="radio" size="16" />
      </control-link>
      <control-link
        v-if="settingsStore.show_menu_item_files"
        class="navbar-item"
        :to="{ name: 'files' }"
      >
        <mdicon class="icon" name="folder-open" size="16" />
      </control-link>
      <control-link
        v-if="settingsStore.show_menu_item_search"
        class="navbar-item"
        :to="{ name: searchStore.search_source }"
      >
        <mdicon class="icon" name="magnify" size="16" />
      </control-link>
    </div>
    <div class="navbar-end">
      <a
        class="navbar-item"
        @click="uiStore.show_burger_menu = !uiStore.show_burger_menu"
      >
        <mdicon
          class="icon"
          :name="uiStore.show_burger_menu ? 'close' : 'menu'"
        />
      </a>
      <div
        class="dropdown is-right"
        :class="{ 'is-active': uiStore.show_burger_menu }"
      >
        <div class="dropdown-menu">
          <div class="dropdown-content">
            <control-link class="dropdown-item" :to="{ name: 'playlists' }">
              <span class="icon-text">
                <mdicon class="icon" name="music-box-multiple" size="16" />
              </span>
              <b v-text="$t('navigation.playlists')" />
            </control-link>
            <control-link class="dropdown-item" :to="{ name: 'music' }">
              <span class="icon-text">
                <mdicon class="icon" name="music" size="16" />
              </span>
              <b v-text="$t('navigation.music')" />
            </control-link>
            <control-link class="dropdown-item" :to="{ name: 'music-artists' }">
              <span class="pl-5" v-text="$t('navigation.artists')" />
            </control-link>
            <control-link class="dropdown-item" :to="{ name: 'music-albums' }">
              <span class="pl-5" v-text="$t('navigation.albums')" />
            </control-link>
            <control-link class="dropdown-item" :to="{ name: 'music-genres' }">
              <span class="pl-5" v-text="$t('navigation.genres')" />
            </control-link>
            <control-link
              v-if="spotify_enabled"
              class="dropdown-item"
              :to="{ name: 'music-spotify' }"
            >
              <span class="pl-5" v-text="$t('navigation.spotify')" />
            </control-link>
            <control-link class="dropdown-item" :to="{ name: 'podcasts' }">
              <span class="icon-text">
                <mdicon class="icon" name="microphone" size="16" />
              </span>
              <b v-text="$t('navigation.podcasts')" />
            </control-link>
            <control-link class="dropdown-item" :to="{ name: 'audiobooks' }">
              <span class="icon-text">
                <mdicon class="icon" name="book-open-variant" size="16" />
              </span>
              <b v-text="$t('navigation.audiobooks')" />
            </control-link>
            <control-link class="dropdown-item" :to="{ name: 'radio' }">
              <span class="icon-text">
                <mdicon class="icon" name="radio" size="16" />
              </span>
              <b v-text="$t('navigation.radio')" />
            </control-link>
            <control-link class="dropdown-item" :to="{ name: 'files' }">
              <span class="icon-text">
                <mdicon class="icon" name="folder-open" size="16" />
              </span>
              <b v-text="$t('navigation.files')" />
            </control-link>
            <control-link
              class="dropdown-item"
              :to="{ name: searchStore.search_source }"
            >
              <span class="icon-text">
                <mdicon class="icon" name="magnify" size="16" />
              </span>
              <b v-text="$t('navigation.search')" />
            </control-link>
            <hr class="my-3" />
            <control-link
              class="dropdown-item"
              :to="{ name: 'settings-webinterface' }"
            >
              {{ $t('navigation.settings') }}
            </control-link>
            <a
              class="dropdown-item"
              @click.stop.prevent="open_update_dialog()"
              v-text="$t('navigation.update-library')"
            />
            <control-link class="dropdown-item" :to="{ name: 'about' }">
              {{ $t('navigation.about') }}
            </control-link>
          </div>
        </div>
      </div>
    </div>
    <div
      v-show="show_settings_menu"
      class="is-overlay"
      @click="show_settings_menu = false"
    />
  </nav>
</template>

<script>
import ControlLink from '@/components/ControlLink.vue'
import { useSearchStore } from '@/stores/search'
import { useServicesStore } from '@/stores/services'
import { useSettingsStore } from '@/stores/settings'
import { useUIStore } from '@/stores/ui'

export default {
  name: 'NavbarTop',
  components: { ControlLink },

  setup() {
    return {
      searchStore: useSearchStore(),
      servicesStore: useServicesStore(),
      settingsStore: useSettingsStore(),
      uiStore: useUIStore()
    }
  },

  data() {
    return {
      show_settings_menu: false
    }
  },

  computed: {
    spotify_enabled() {
      return this.servicesStore.spotify.webapi_token_valid
    },
    zindex() {
      if (this.uiStore.show_player_menu) {
        return 'z-index: 21'
      }
      return ''
    }
  },

  watch: {
    $route(to, from) {
      this.show_settings_menu = false
    }
  },

  methods: {
    on_click_outside_settings() {
      this.show_settings_menu = !this.show_settings_menu
    },
    open_update_dialog() {
      this.uiStore.show_update_dialog = true
      this.show_settings_menu = false
      this.uiStore.show_burger_menu = false
    }
  }
}
</script>
