<template>
  <nav class="navbar is-fixed-top is-top">
    <div class="navbar-brand is-fullwidth">
      <div class="is-flex is-clipped">
        <control-link
          v-for="menu in menus.filter((menu) => menu.show && !menu.sub)"
          :key="menu.name"
          :to="{ name: menu.name }"
          class="navbar-item"
        >
          <mdicon class="icon" :name="menu.icon" size="16" />
        </control-link>
      </div>
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
            <template v-for="menu in menus" :key="menu.name">
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

<script setup>
import ControlLink from '@/components/ControlLink.vue'
import { computed } from 'vue'
import { useSearchStore } from '@/stores/search'
import { useServicesStore } from '@/stores/services'
import { useSettingsStore } from '@/stores/settings'
import { useUIStore } from '@/stores/ui'

const searchStore = useSearchStore()
const servicesStore = useServicesStore()
const settingsStore = useSettingsStore()
const uiStore = useUIStore()

const openUpdateDialog = () => {
  uiStore.showUpdateDialog = true
  uiStore.hideMenus()
}

const menus = computed(() => [
  {
    icon: 'music-box-multiple',
    key: 'navigation.playlists',
    name: 'playlists',
    show: settingsStore.showMenuItemPlaylists
  },
  {
    icon: 'music',
    key: 'navigation.music',
    name: 'music',
    show: settingsStore.showMenuItemMusic
  },
  { key: 'navigation.artists', name: 'music-artists', sub: true },
  { key: 'navigation.albums', name: 'music-albums', sub: true },
  { key: 'navigation.genres', name: 'music-genres', sub: true },
  { key: 'navigation.composers', name: 'music-composers', sub: true },
  {
    key: 'navigation.spotify',
    name: 'music-spotify',
    show: servicesStore.isSpotifyActive,
    sub: true
  },
  {
    icon: 'microphone',
    key: 'navigation.podcasts',
    name: 'podcasts',
    show: settingsStore.showMenuItemPodcasts
  },
  {
    icon: 'book-open-variant',
    key: 'navigation.audiobooks',
    name: 'audiobooks',
    show: settingsStore.showMenuItemAudiobooks
  },
  {
    icon: 'radio',
    key: 'navigation.radio',
    name: 'radio',
    show: settingsStore.showMenuItemRadio
  },
  {
    icon: 'folder-open',
    key: 'navigation.files',
    name: 'files',
    show: settingsStore.showMenuItemFiles
  },
  {
    icon: 'magnify',
    key: 'navigation.search',
    name: searchStore.source,
    show: settingsStore.showMenuItemSearch
  },
  { separator: true },
  { key: 'navigation.settings', name: 'settings-webinterface' },
  { key: 'navigation.outputs', name: 'outputs' },
  { action: openUpdateDialog, key: 'navigation.update-library' },
  { key: 'navigation.about', name: 'about' }
])
</script>

<style scoped>
.is-fullwidth {
  width: 100%;
}
</style>
