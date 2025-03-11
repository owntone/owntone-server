import { defineStore } from 'pinia'

export const useServicesStore = defineStore('ServicesStore', {
  actions: {
    isSpotifyEnabled() {
      return this.spotify.webapi_token_valid
    }
  },
  state: () => ({
    lastfm: {},
    spotify: {}
  })
})
