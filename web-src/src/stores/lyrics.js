import { defineStore } from 'pinia'

export const useLyricsStore = defineStore('LyricsStore', {
  actions: {
    toggle() {
      this.active = !this.active
    }
  },
  state: () => ({
    content: [],
    active: false
  })
})
