import { defineStore } from 'pinia'
import services from '@/api/services'

export const useServicesStore = defineStore('ServicesStore', {
  actions: {
    async initialiseLastfm() {
      this.lastfm = await services.lastfm()
    },
    initialiseSpotify() {
      services.spotify().then((data) => {
        this.spotify = data
        if (this.spotifyTimerId > 0) {
          clearTimeout(this.spotifyTimerId)
          this.spotifyTimerId = 0
        }
        if (data.webapi_token_expires_in > 0 && data.webapi_token) {
          this.spotifyTimerId = setTimeout(
            () => this.initialiseSpotify(),
            1000 * data.webapi_token_expires_in
          )
        }
      })
    }
  },
  getters: {
    hasMissingSpotifyScopes: (state) => state.missingSpotifyScopes.length > 0,
    isAuthorizationRequired: (state) =>
      !state.isSpotifyActive || state.hasMissingSpotifyScopes,
    isLastfmActive: (state) => state.lastfm.scrobbling_enabled,
    isLastfmEnabled: (state) => state.lastfm.enabled,
    isSpotifyActive: (state) => state.spotify.webapi_token_valid,
    isSpotifyEnabled: (state) => state.spotify.spotify_installed,
    grantedSpotifyScopes: (state) =>
      state.spotify.webapi_granted_scope?.split(' ') ?? [],
    missingSpotifyScopes(state) {
      const scopes = new Set(state.grantedSpotifyScopes)
      return (
        state.requiredSpotifyScopes.filter((scope) => !scopes.has(scope)) ?? []
      )
    },
    requiredSpotifyScopes: (state) =>
      state.spotify.webapi_required_scope?.split(' ') ?? []
  },
  state: () => ({
    lastfm: {},
    spotify: {},
    spotifyTimerId: 0
  })
})
