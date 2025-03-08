import { defineStore } from 'pinia'

export const useSearchStore = defineStore('SearchStore', {
  actions: {
    add(query) {
      const index = this.history.indexOf(query)
      if (index !== -1) {
        this.history.splice(index, 1)
      }
      this.history.unshift(query)
      if (this.history.length > 5) {
        this.history.pop()
      }
    },
    remove(query) {
      const index = this.history.indexOf(query)
      if (index !== -1) {
        this.history.splice(index, 1)
      }
    }
  },
  state: () => ({
    history: [],
    query: '',
    source: 'search-library'
  })
})
