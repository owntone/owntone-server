import { defineStore } from 'pinia'

export const useLyricsStore = defineStore('LyricsStore', {
  state: () => ({
    content: [],
    pane: false
  })
})
