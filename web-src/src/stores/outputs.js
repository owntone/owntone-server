import { defineStore } from 'pinia'

export const useOutputsStore = defineStore('OutputsStore', {
  state: () => ({
    outputs: []
  })
})
