import { SpotifyApi } from '@spotify/web-api-ts-sdk'
import api from '@/api'

export default {
  lastfm() {
    return api.get('./api/lastfm')
  },
  loginLastfm(credentials) {
    return api.post('./api/lastfm-login', credentials)
  },
  logoutLastfm() {
    return api.get('./api/lastfm-logout')
  },
  logoutSpotify() {
    return api.get('./api/spotify-logout')
  },
  spotify() {
    return api.get('./api/spotify').then((configuration) => {
      const sdk = SpotifyApi.withAccessToken(configuration.webapi_client_id, {
        access_token: configuration.webapi_token,
        token_type: 'Bearer'
      })
      return { api: sdk, configuration }
    })
  }
}
