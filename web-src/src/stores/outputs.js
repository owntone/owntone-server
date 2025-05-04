import { defineStore } from 'pinia'
import outputs from '@/api/outputs'

export const useOutputsStore = defineStore('OutputsStore', {
  actions: {
    async initialise() {
      this.$state = await outputs.state()
    }
  },
  state: () => ({
    outputs: []
  })
})
