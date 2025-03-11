import { defineStore } from 'pinia'

export const usePlayerStore = defineStore('PlayerStore', {
  getters: {
    isPlaying: (state) => state.state === 'play',
    isRepeatAll: (state) => state.repeat === 'all',
    isRepeatOff: (state) => state.repeat === 'off',
    isRepeatSingle: (state) => state.repeat === 'single',
    isStopped: (state) => state.state === 'stop'
  },
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
