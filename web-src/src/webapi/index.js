import axios from 'axios'
import i18n from '@/i18n'
import store from '@/store'

const { t } = i18n.global

axios.interceptors.response.use(
  (response) => response,
  (error) => {
    if (error.request.status && error.request.responseURL) {
      store.dispatch('add_notification', {
        text: t('server.request-failed', {
          cause: error.request.statusText,
          status: error.request.status,
          url: error.request.responseURL
        }),
        type: 'danger'
      })
    }
    return Promise.reject(error)
  }
)

export default {
  config() {
    return axios.get('./api/config')
  },

  lastfm() {
    return axios.get('./api/lastfm')
  },

  lastfm_login(credentials) {
    return axios.post('./api/lastfm-login', credentials)
  },

  lastfm_logout() {
    return axios.get('./api/lastfm-logout')
  },

  library_add(url) {
    return axios.post('./api/library/add', undefined, { params: { url } })
  },

  library_album(albumId) {
    return axios.get(`./api/library/albums/${albumId}`)
  },

  library_album_track_update(albumId, attributes) {
    return axios.put(`./api/library/albums/${albumId}/tracks`, undefined, {
      params: attributes
    })
  },

  library_album_tracks(albumId, filter = { limit: -1, offset: 0 }) {
    return axios.get(`./api/library/albums/${albumId}/tracks`, {
      params: filter
    })
  },

  library_albums(media_kind = undefined) {
    return axios.get('./api/library/albums', { params: { media_kind } })
  },

  library_artist(artistId) {
    return axios.get(`./api/library/artists/${artistId}`)
  },

  library_artist_albums(artistId) {
    return axios.get(`./api/library/artists/${artistId}/albums`)
  },

  library_artist_tracks(artist) {
    if (artist) {
      const params = {
        expression: `songartistid is "${artist}"`,
        type: 'tracks'
      }
      return axios.get('./api/search', { params })
    }
  },

  library_artists(media_kind = undefined) {
    return axios.get('./api/library/artists', { params: { media_kind } })
  },

  library_composer(composer) {
    return axios.get(`./api/library/composers/${encodeURIComponent(composer)}`)
  },

  library_composer_albums(composer) {
    const params = {
      expression: `composer is "${composer}" and media_kind is music`,
      type: 'albums'
    }
    return axios.get('./api/search', { params })
  },

  library_composer_tracks(composer) {
    const params = {
      expression: `composer is "${composer}" and media_kind is music`,
      type: 'tracks'
    }
    return axios.get('./api/search', { params })
  },

  library_composers(media_kind = undefined) {
    return axios.get('./api/library/composers', { params: { media_kind } })
  },

  library_count(expression) {
    return axios.get(`./api/library/count?expression=${expression}`)
  },

  library_files(directory = undefined) {
    return axios.get('./api/library/files', { params: { directory } })
  },

  library_genre(genre, media_kind = undefined) {
    return axios.get(`./api/library/genres/${encodeURIComponent(genre)}`, {
      params: { media_kind }
    })
  },

  library_genre_albums(genre, media_kind) {
    const params = {
      expression: `genre is "${genre}" and media_kind is ${media_kind}`,
      type: 'albums'
    }
    return axios.get('./api/search', { params })
  },

  library_genre_tracks(genre, media_kind) {
    const params = {
      expression: `genre is "${genre}" and media_kind is ${media_kind}`,
      type: 'tracks'
    }
    return axios.get('./api/search', { params })
  },

  library_genres(media_kind = undefined) {
    return axios.get('./api/library/genres', { params: { media_kind } })
  },

  library_playlist(playlistId) {
    return axios.get(`./api/library/playlists/${playlistId}`)
  },

  library_playlist_delete(playlistId) {
    return axios.delete(`./api/library/playlists/${playlistId}`, undefined)
  },

  library_playlist_folder(playlistId = 0) {
    return axios.get(`./api/library/playlists/${playlistId}/playlists`)
  },

  library_playlist_tracks(playlistId) {
    return axios.get(`./api/library/playlists/${playlistId}/tracks`)
  },

  library_playlists() {
    return axios.get('./api/library/playlists')
  },

  library_podcast_episodes(albumId) {
    const params = {
      expression: `media_kind is podcast and songalbumid is "${albumId}" ORDER BY date_released DESC`,
      type: 'tracks'
    }
    return axios.get('./api/search', { params })
  },

  library_podcasts_new_episodes() {
    const params = {
      expression:
        'media_kind is podcast and play_count = 0 ORDER BY time_added DESC',
      type: 'tracks'
    }
    return axios.get('./api/search', { params })
  },

  library_radio_streams() {
    const params = {
      expression: 'data_kind is url and song_length = 0',
      media_kind: 'music',
      type: 'tracks'
    }
    return axios.get('./api/search', { params })
  },

  library_rescan(scanKind) {
    const params = {}
    if (scanKind) {
      params.scan_kind = scanKind
    }
    return axios.put('./api/rescan', undefined, { params })
  },

  library_stats() {
    return axios.get('./api/library')
  },

  library_track(trackId) {
    return axios.get(`./api/library/tracks/${trackId}`)
  },

  library_track_playlists(trackId) {
    return axios.get(`./api/library/tracks/${trackId}/playlists`)
  },

  library_track_update(trackId, attributes = {}) {
    return axios.put(`./api/library/tracks/${trackId}`, undefined, {
      params: attributes
    })
  },

  library_update(scanKind) {
    const params = {}
    if (scanKind) {
      params.scan_kind = scanKind
    }
    return axios.put('./api/update', undefined, { params })
  },

  output_toggle(outputId) {
    return axios.put(`./api/outputs/${outputId}/toggle`)
  },

  output_update(outputId, output) {
    return axios.put(`./api/outputs/${outputId}`, output)
  },

  outputs() {
    return axios.get('./api/outputs')
  },

  pairing() {
    return axios.get('./api/pairing')
  },

  pairing_kickoff(pairingReq) {
    return axios.post('./api/pairing', pairingReq)
  },

  player_consume(state) {
    return axios.put(`./api/player/consume?state=${state}`)
  },

  player_next() {
    return axios.put('./api/player/next')
  },

  player_output_volume(outputId, outputVolume) {
    return axios.put(
      `./api/player/volume?volume=${outputVolume}&output_id=${outputId}`
    )
  },

  player_pause() {
    return axios.put('./api/player/pause')
  },

  player_play(options = {}) {
    return axios.put('./api/player/play', undefined, { params: options })
  },

  player_play_expression(expression, shuffle, position = undefined) {
    const params = {
      clear: 'true',
      expression,
      playback: 'start',
      playback_from_position: position,
      shuffle: `${shuffle}`
    }
    return axios.post('./api/queue/items/add', undefined, { params })
  },

  player_play_uri(uris, shuffle, position = undefined) {
    const params = {
      clear: 'true',
      playback: 'start',
      playback_from_position: position,
      shuffle: `${shuffle}`,
      uris
    }
    return axios.post('./api/queue/items/add', undefined, { params })
  },

  player_playid(itemId) {
    return axios.put(`./api/player/play?item_id=${itemId}`)
  },

  player_playpos(position) {
    return axios.put(`./api/player/play?position=${position}`)
  },

  player_previous() {
    return axios.put('./api/player/previous')
  },

  player_repeat(mode) {
    return axios.put(`./api/player/repeat?state=${mode}`)
  },

  player_seek(seekMs) {
    return axios.put(`./api/player/seek?seek_ms=${seekMs}`)
  },

  player_seek_to_pos(position) {
    return axios.put(`./api/player/seek?position_ms=${position}`)
  },

  player_shuffle(state) {
    return axios.put(`./api/player/shuffle?state=${state}`)
  },

  player_status() {
    return axios.get('./api/player')
  },

  player_stop() {
    return axios.put('./api/player/stop')
  },

  player_volume(volume) {
    return axios.put(`./api/player/volume?volume=${volume}`)
  },

  queue() {
    return axios.get('./api/queue')
  },

  queue_add(uri) {
    return axios.post(`./api/queue/items/add?uris=${uri}`).then((response) => {
      store.dispatch('add_notification', {
        text: t('server.appended-tracks', { count: response.data.count }),
        timeout: 2000,
        type: 'info'
      })
      return Promise.resolve(response)
    })
  },

  queue_add_next(uri) {
    let position = 0
    if (store.getters.now_playing && store.getters.now_playing.id) {
      position = store.getters.now_playing.position + 1
    }
    return axios
      .post(`./api/queue/items/add?uris=${uri}&position=${position}`)
      .then((response) => {
        store.dispatch('add_notification', {
          text: t('server.appended-tracks', {
            count: response.data.count
          }),
          timeout: 2000,
          type: 'info'
        })
        return Promise.resolve(response)
      })
  },

  queue_clear() {
    return axios.put('./api/queue/clear')
  },

  queue_expression_add(expression) {
    return axios
      .post('./api/queue/items/add', undefined, { params: { expression } })
      .then((response) => {
        store.dispatch('add_notification', {
          text: t('server.appended-tracks', {
            count: response.data.count
          }),
          timeout: 2000,
          type: 'info'
        })
        return Promise.resolve(response)
      })
  },

  queue_expression_add_next(expression) {
    const params = {}
    params.expression = expression
    params.position = 0
    if (store.getters.now_playing && store.getters.now_playing.id) {
      params.position = store.getters.now_playing.position + 1
    }
    return axios
      .post('./api/queue/items/add', undefined, { params })
      .then((response) => {
        store.dispatch('add_notification', {
          text: t('server.appended-tracks', {
            count: response.data.count
          }),
          timeout: 2000,
          type: 'info'
        })
        return Promise.resolve(response)
      })
  },

  queue_move(itemId, position) {
    return axios.put(`./api/queue/items/${itemId}?new_position=${position}`)
  },

  queue_remove(itemId) {
    return axios.delete(`./api/queue/items/${itemId}`)
  },

  queue_save_playlist(name) {
    return axios
      .post('./api/queue/save', undefined, { params: { name } })
      .then((response) => {
        store.dispatch('add_notification', {
          text: t('server.queue-saved', { name }),
          timeout: 2000,
          type: 'info'
        })
        return Promise.resolve(response)
      })
  },

  search(params) {
    return axios.get('./api/search', { params })
  },

  settings() {
    return axios.get('./api/settings')
  },

  settings_update(categoryName, option) {
    return axios.put(`./api/settings/${categoryName}/${option.name}`, option)
  },

  spotify() {
    return axios.get('./api/spotify')
  },

  spotify_logout() {
    return axios.get('./api/spotify-logout')
  }
}
