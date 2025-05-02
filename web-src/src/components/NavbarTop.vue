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
      <a class="navbar-item ml-auto" @click="uiStore.toggleBurgerMenu">
        <mdicon
          class="icon"
          :name="uiStore.showBurgerMenu ? 'close' : 'menu'"
        />
      </a>
      <div
        class="dropdown is-right"
        :class="{ 'is-active': uiStore.showBurgerMenu }"
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
                v-text="$t(menu.key)"
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
                  v-text="$t(menu.key)"
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
          icon: 'music-box-multiple',
          key: 'navigation.playlists',
          name: 'playlists',
          show: this.settingsStore.showMenuItemPlaylists
        },
        {
          icon: 'music',
          key: 'navigation.music',
          name: 'music',
          show: this.settingsStore.showMenuItemMusic
        },
        {
          key: 'navigation.artists',
          name: 'music-artists',
          show: true,
          sub: true
        },
        {
          key: 'navigation.albums',
          name: 'music-albums',
          show: true,
          sub: true
        },
        {
          key: 'navigation.genres',
          name: 'music-genres',
          show: true,
          sub: true
        },
        {
          key: 'navigation.composers',
          name: 'music-composers',
          show: true,
          sub: true
        },
        {
          key: 'navigation.spotify',
          name: 'music-spotify',
          show: this.servicesStore.isSpotifyActive,
          sub: true
        },
        {
          icon: 'microphone',
          key: 'navigation.podcasts',
          name: 'podcasts',
          show: this.settingsStore.showMenuItemPodcasts
        },
        {
          icon: 'book-open-variant',
          key: 'navigation.audiobooks',
          name: 'audiobooks',
          show: this.settingsStore.showMenuItemAudiobooks
        },
        {
          icon: 'radio',
          key: 'navigation.radio',
          name: 'radio',
          show: this.settingsStore.showMenuItemRadio
        },
        {
          icon: 'folder-open',
          key: 'navigation.files',
          name: 'files',
          show: this.settingsStore.showMenuItemFiles
        },
        {
          icon: 'magnify',
          key: 'navigation.search',
          name: this.searchStore.source,
          show: this.settingsStore.showMenuItemSearch
        },
        { separator: true, show: true },
        {
          key: 'navigation.settings',
          name: 'settings-webinterface',
          show: true
        },
        {
          action: this.openUpdateDialog,
          key: 'navigation.update-library',
          show: true
        },
        { key: 'navigation.about', name: 'about', show: true }
      ]
    },
    zindex() {
      if (this.uiStore.showPlayerMenu) {
        return 'z-index: 21'
      }
      return ''
    }
  },
  methods: {
    openUpdateDialog() {
      this.uiStore.showUpdateDialog = true
      this.uiStore.hideMenus()
    }
  }
}
</script>
