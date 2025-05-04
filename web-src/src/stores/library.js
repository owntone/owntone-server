import { defineStore } from 'pinia'
import library from '@/api/library'

export const useLibraryStore = defineStore('LibraryStore', {
  actions: {
    async initialise() {
      this.$state = await library.state()
    }
  },
  state: () => ({
    albums: 0,
    artists: 0,
    db_playtime: 0,
    songs: 0,
    started_at: '01',
    update_dialog_scan_kind: '',
    updated_at: '01',
    updating: false
  })
})
