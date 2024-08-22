import { defineStore } from 'pinia'

export const usePlayerStore = defineStore('PlayerStore', {
  state: () => ({
    consume: false,
    item_id: 0,
    item_length_ms: 0,
    item_progress_ms: 0,
    repeat: 'off',
    shuffle: false,
    state: 'stop',
    volume: 0
  })
})
