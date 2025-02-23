<template>
  <nav class="navbar is-fixed-top is-light" :style="zindex">
    <div class="navbar-brand is-flex-grow-1">
      <control-link
        v-for="menu in menus.filter((menu) => menu.show && menu.icon)"
        :key="menu.name"
        :to="{ name: menu.name }"
        class="navbar-item"
      >
        <mdicon class="icon" :name="menu.icon" size="16" />
      </control-link>
      <a
        class="navbar-item ml-auto"
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
        <div class="dropdown-menu is-mobile">
          <div class="dropdown-content">
            <template
              v-for="menu in menus.filter((menu) => menu.show)"
              :key="menu.name"
            >
              <hr v-if="menu.separator" class="my-3" />
              <a
                v-else-if="menu.action"
                class="dropdown-item"
                @click.stop.prevent="menu.action"
                v-text="$t(menu.label)"
              />
              <control-link
                v-else
                :to="{ name: menu.name }"
                class="dropdown-item"
              >
                <span v-if="menu.icon" class="icon-text">
                  <mdicon class="icon" :name="menu.icon" size="16" />
                </span>
                <span
                  :class="{
                    'pl-5': menu.sub,
                    'has-text-weight-semibold': menu.icon
                  }"
                  v-text="$t(menu.label)"
                />
              </control-link>
            </template>
          </div>
        </div>
      </div>
    </div>
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
  computed: {
    menus() {
      return [
        {
          name: 'playlists',
          label: 'navigation.playlists',
          icon: 'music-box-multiple',
          show: this.settingsStore.show_menu_item_playlists
        },
        {
          name: 'music',
          label: 'navigation.music',
          icon: 'music',
          show: this.settingsStore.show_menu_item_music
        },
        {
          name: 'music-artists',
          label: 'navigation.artists',
          show: true,
          sub: true
        },
        {
          name: 'music-albums',
          label: 'navigation.albums',
          show: true,
          sub: true
        },
        {
          name: 'music-genres',
          label: 'navigation.genres',
          show: true,
          sub: true
        },
        {
          name: 'music-spotify',
          label: 'navigation.spotify',
          show: this.servicesStore.spotify.webapi_token_valid,
          sub: true
        },
        {
          name: 'podcasts',
          label: 'navigation.podcasts',
          icon: 'microphone',
          show: this.settingsStore.show_menu_item_podcasts
        },
        {
          name: 'audiobooks',
          label: 'navigation.audiobooks',
          icon: 'book-open-variant',
          show: this.settingsStore.show_menu_item_audiobooks
        },
        {
          name: 'radio',
          label: 'navigation.radio',
          icon: 'radio',
          show: this.settingsStore.show_menu_item_radio
        },
        {
          name: 'files',
          label: 'navigation.files',
          icon: 'folder-open',
          show: this.settingsStore.show_menu_item_files
        },
        {
          name: this.searchStore.search_source,
          label: 'navigation.search',
          icon: 'magnify',
          show: this.settingsStore.show_menu_item_search
        },
        { separator: true, show: true },
        {
          name: 'settings-webinterface',
          label: 'navigation.settings',
          show: true
        },
        {
          label: 'navigation.update-library',
          action: this.open_update_dialog,
          show: true
        },
        { name: 'about', label: 'navigation.about', show: true }
      ]
    },
    zindex() {
      if (this.uiStore.show_player_menu) {
        return 'z-index: 21'
      }
      return ''
    }
  },
  methods: {
    open_update_dialog() {
      this.uiStore.show_update_dialog = true
      this.uiStore.show_burger_menu = false
    }
  }
}
</script>
