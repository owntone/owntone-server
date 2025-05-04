import { defineStore } from 'pinia'
import remotes from '@/api/remotes'

export const useRemotesStore = defineStore('RemotesStore', {
  actions: {
    async initialise() {
      this.$state = await remotes.state()
    }
  },
  state: () => ({
    active: false,
    remote: ''
  })
})
