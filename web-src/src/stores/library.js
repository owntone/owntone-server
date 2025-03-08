import { defineStore } from 'pinia'

export const useLibraryStore = defineStore('LibraryStore', {
  state: () => ({
    albums: 0,
    artists: 0,
    db_playtime: 0,
    rss: {},
    songs: 0,
    started_at: '01',
    update_dialog_scan_kind: '',
    updated_at: '01',
    updating: false
  })
})
