import { defineStore } from 'pinia'
import { usePlayerStore } from '@/stores/player'

export const useQueueStore = defineStore('QueueStore', {
  state: () => ({
    count: 0,
    items: [],
    version: 0
  }),
  getters: {
    current(state) {
      const player = usePlayerStore()
      return state.items.find((item) => item.id === player.item_id) ?? {}
    }
  }
})
