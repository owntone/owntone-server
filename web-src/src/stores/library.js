import { defineStore } from 'pinia'

export const useLibraryStore = defineStore('LibraryStore', {
  state: () => ({
    albums: 0,
    artists: 0,
    db_playtime: 0,
    songs: 0,
    rss: {},
    started_at: '01',
    updated_at: '01',
    update_dialog_scan_kind: '',
    updating: false
  })
})
