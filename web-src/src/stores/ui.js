import { defineStore } from 'pinia'

export const useUIStore = defineStore('UIStore', {
  actions: {
    hideMenus() {
      this.showBurgerMenu = false
      this.showPlayerMenu = false
    },
    toggleBurgerMenu() {
      this.showPlayerMenu = false
      this.showBurgerMenu = !this.showBurgerMenu
    },
    togglePlayerMenu() {
      this.showBurgerMenu = false
      this.showPlayerMenu = !this.showPlayerMenu
    },
    toggleHideReadItems() {
      this.hideReadItems = !this.hideReadItems
    }
  },
  state: () => ({
    albums_sort: 1,
    artist_albums_sort: 1,
    artist_tracks_sort: 1,
    artists_sort: 1,
    composer_tracks_sort: 1,
    genre_tracks_sort: 1,
    hideReadItems: false,
    hideSingles: false,
    hideSpotify: false,
    showBurgerMenu: false,
    showPlayerMenu: false,
    showUpdateDialog: false
  })
})
