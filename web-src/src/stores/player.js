import { defineStore } from 'pinia'
import library from '@/api/library'
import player from '@/api/player'
import { useQueueStore } from '@/stores/queue'

export const usePlayerStore = defineStore('PlayerStore', {
  actions: {
    async initialise() {
      this.$state = await player.state()
      const queueStore = useQueueStore()
      if (queueStore.current.track_id) {
        library.track(queueStore.current.track_id).then((data) => {
          this.lyricsContent = data.lyrics || ''
        })
      }
    }
  },
  getters: {
    isMuted: (state) => state.volume === 0,
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
    lyricsContent: '',
    repeat: 'off',
    showLyrics: false,
    shuffle: false,
    state: 'stop',
    volume: 0
  })
})
