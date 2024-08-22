import { defineStore } from 'pinia'

export const useSearchStore = defineStore('SearchStore', {
  state: () => ({
    recent_searches: [],
    search_query: '',
    search_source: 'search-library'
  }),
  actions: {
    add(query) {
      const index = this.recent_searches.indexOf(query)
      if (index !== -1) {
        this.recent_searches.splice(index, 1)
      }
      this.recent_searches.unshift(query)
      if (this.recent_searches.length > 5) {
        this.recent_searches.pop()
      }
    },
    remove(query) {
      const index = this.recent_searches.indexOf(query)
      if (index !== -1) {
        this.recent_searches.splice(index, 1)
      }
    }
  }
})
