import { defineStore } from 'pinia'

export const useServicesStore = defineStore('ServicesStore', {
  state: () => ({
    lastfm: {},
    spotify: {}
  })
})
