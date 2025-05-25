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
    toggleHideReadItems() {
      this.hideReadItems = !this.hideReadItems
    },
    togglePlayerMenu() {
      this.showBurgerMenu = false
      this.showPlayerMenu = !this.showPlayerMenu
    }
  },
  state: () => ({
    albumsSort: 1,
    artistAlbumsSort: 1,
    artistTracksSort: 1,
    artistsSort: 1,
    composerTracksSort: 1,
    genreTracksSort: 1,
    hideReadItems: false,
    hideSingles: false,
    hideSpotify: false,
    showBurgerMenu: false,
    showPlayerMenu: false,
    showUpdateDialog: false
  })
})
