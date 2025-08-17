import { defineStore } from 'pinia'
import services from '@/api/services'

export const useServicesStore = defineStore('ServicesStore', {
  actions: {
    async initialiseLastfm() {
      this.lastfm = await services.lastfm()
    },
    initialiseSpotify() {
      services.spotify().then(({ configuration }) => {
        this.spotify = configuration
      })
    }
  },
  getters: {
    grantedSpotifyScopes: (state) =>
      state.spotify.webapi_granted_scope?.split(' ') ?? [],
    hasMissingSpotifyScopes: (state) => state.missingSpotifyScopes.length > 0,
    isAuthorizationRequired: (state) =>
      !state.isSpotifyActive || state.hasMissingSpotifyScopes,
    isLastfmActive: (state) => state.lastfm.scrobbling_enabled,
    isLastfmEnabled: (state) => state.lastfm.enabled,
    isSpotifyActive: (state) => state.spotify.webapi_token_valid,
    isSpotifyEnabled: (state) => state.spotify.spotify_installed,
    missingSpotifyScopes(state) {
      const scopes = new Set(state.grantedSpotifyScopes)
      return (
        state.requiredSpotifyScopes.filter((scope) => !scopes.has(scope)) ?? []
      )
    },
    requiredSpotifyScopes: (state) =>
      state.spotify.webapi_required_scope?.split(' ') ?? []
  },
  state: () => ({ lastfm: {}, spotify: {} })
})
