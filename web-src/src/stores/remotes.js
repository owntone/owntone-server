import { defineStore } from 'pinia'

export const useRemotesStore = defineStore('RemotesStore', {
  state: () => ({
    active: false,
    remote: ''
  })
})
