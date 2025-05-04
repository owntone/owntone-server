import { defineStore } from 'pinia'
import queue from '@/api/queue'
import { useConfigurationStore } from '@/stores/configuration'
import { usePlayerStore } from '@/stores/player'

export const useQueueStore = defineStore('QueueStore', {
  actions: {
    async initialise() {
      this.$state = await queue.state()
    }
  },
  getters: {
    current(state) {
      const player = usePlayerStore()
      return state.items.find((item) => item.id === player.item_id) ?? {}
    },
    isEmpty(state) {
      return state.items.length === 0
    },
    isPauseAllowed(state) {
      return state.current && state.current.data_kind !== 'pipe'
    },
    isSavingAllowed() {
      const configuration = useConfigurationStore()
      return (
        configuration.allow_modifying_stored_playlists &&
        configuration.default_playlist_directory
      )
    }
  },
  state: () => ({
    count: 0,
    items: [],
    version: 0
  })
})
