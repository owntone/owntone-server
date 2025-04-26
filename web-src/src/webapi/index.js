import axios from 'axios'
import i18n from '@/i18n'
import { useNotificationsStore } from '@/stores/notifications'
import { useQueueStore } from '@/stores/queue'

const { t } = i18n.global

axios.interceptors.response.use(
  (response) => response.data,
  (error) => {
    if (error.request.status && error.request.responseURL) {
      useNotificationsStore().add({
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
    return axios.post('./api/library/add', null, { params: { url } })
  },

  library_album(albumId) {
    return axios.get(`./api/library/albums/${albumId}`)
  },

  library_album_track_update(albumId, attributes) {
    return axios.put(`./api/library/albums/${albumId}/tracks`, null, {
      params: attributes
    })
  },

  library_album_tracks(albumId, filter = { limit: -1, offset: 0 }) {
    return axios.get(`./api/library/albums/${albumId}/tracks`, {
      params: filter
    })
  },

  library_albums(media_kind) {
    return axios.get('./api/library/albums', { params: { media_kind } })
  },

  library_artist(artistId) {
    return axios.get(`./api/library/artists/${artistId}`)
  },

  library_artist_albums(artistId) {
    return axios.get(`./api/library/artists/${artistId}/albums`)
  },

  async library_artist_tracks(artist) {
    const params = {
      expression: `songartistid is "${artist}"`,
      type: 'tracks'
    }
    const data = await axios.get('./api/search', { params })
    return data.tracks
  },

  library_artists(media_kind) {
    return axios.get('./api/library/artists', { params: { media_kind } })
  },

  library_composer(composer) {
    return axios.get(`./api/library/composers/${encodeURIComponent(composer)}`)
  },

  async library_composer_albums(composer) {
    const params = {
      expression: `composer is "${composer}" and media_kind is music`,
      type: 'albums'
    }
    const data = await axios.get('./api/search', { params })
    return data.albums
  },

  async library_composer_tracks(composer) {
    const params = {
      expression: `composer is "${composer}" and media_kind is music`,
      type: 'tracks'
    }
    const data = await axios.get('./api/search', { params })
    return data.tracks
  },

  library_composers(media_kind) {
    return axios.get('./api/library/composers', { params: { media_kind } })
  },

  library_count(expression) {
    return axios.get(`./api/library/count?expression=${expression}`)
  },

  library_files(directory) {
    return axios.get('./api/library/files', { params: { directory } })
  },

  async library_genre(genre, media_kind) {
    const params = {
      expression: `genre is "${genre}" and media_kind is ${media_kind}`,
      type: 'genres'
    }
    const data = await axios.get('./api/search', { params })
    return data.genres
  },

  async library_genre_albums(genre, media_kind) {
    const params = {
      expression: `genre is "${genre}" and media_kind is ${media_kind}`,
      type: 'albums'
    }
    const data = await axios.get('./api/search', { params })
    return data.albums
  },

  async library_genre_tracks(genre, media_kind) {
    const params = {
      expression: `genre is "${genre}" and media_kind is ${media_kind}`,
      type: 'tracks'
    }
    const data = await axios.get('./api/search', { params })
    return data.tracks
  },

  async library_genres(media_kind) {
    const params = {
      expression: `media_kind is ${media_kind}`,
      type: 'genres'
    }
    const data = await axios.get('./api/search', { params })
    return data.genres
  },

  library_playlist(playlistId) {
    return axios.get(`./api/library/playlists/${playlistId}`)
  },

  library_playlist_delete(playlistId) {
    return axios.delete(`./api/library/playlists/${playlistId}`)
  },

  library_playlist_folder(playlistId = 0) {
    return axios.get(`./api/library/playlists/${playlistId}/playlists`)
  },

  library_playlist_tracks(playlistId) {
    return axios.get(`./api/library/playlists/${playlistId}/tracks`)
  },

  async library_podcast_episodes(albumId) {
    const params = {
      expression: `media_kind is podcast and songalbumid is "${albumId}" ORDER BY date_released DESC`,
      type: 'tracks'
    }
    const data = await axios.get('./api/search', { params })
    return data.tracks
  },

  async library_podcasts_new_episodes() {
    const params = {
      expression:
        'media_kind is podcast and play_count = 0 ORDER BY time_added DESC',
      type: 'tracks'
    }
    const data = await axios.get('./api/search', { params })
    return data.tracks
  },

  async library_radio_streams() {
    const params = {
      expression: 'data_kind is url and song_length = 0',
      media_kind: 'music',
      type: 'tracks'
    }
    const data = await axios.get('./api/search', { params })
    return data.tracks
  },

  library_rescan(scan_kind) {
    return axios.put('./api/rescan', null, { params: { scan_kind } })
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
    return axios.put(`./api/library/tracks/${trackId}`, null, {
      params: attributes
    })
  },

  library_update(scan_kind) {
    return axios.put('./api/update', null, { params: { scan_kind } })
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
    return axios.put('./api/player/play', null, { params: options })
  },

  player_play_expression(expression, shuffle, position) {
    const params = {
      clear: 'true',
      expression,
      playback: 'start',
      playback_from_position: position,
      shuffle
    }
    return axios.post('./api/queue/items/add', null, { params })
  },

  player_play_uri(uris, shuffle, position) {
    const params = {
      clear: 'true',
      playback: 'start',
      playback_from_position: position,
      shuffle,
      uris
    }
    return axios.post('./api/queue/items/add', null, { params })
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

  async queue_add(uri) {
    const data = await axios.post(`./api/queue/items/add?uris=${uri}`)
    useNotificationsStore().add({
      text: t('server.appended-tracks', { count: data.count }),
      timeout: 2000,
      type: 'info'
    })
    return await Promise.resolve(data)
  },

  async queue_add_next(uri) {
    let position = 0
    const { current } = useQueueStore()
    if (current?.id) {
      position = current.position + 1
    }
    const data = await axios.post(
      `./api/queue/items/add?uris=${uri}&position=${position}`
    )
    useNotificationsStore().add({
      text: t('server.appended-tracks', { count: data.count }),
      timeout: 2000,
      type: 'info'
    })
    return await Promise.resolve(data)
  },

  queue_clear() {
    return axios.put('./api/queue/clear')
  },

  async queue_expression_add(expression) {
    const data = await axios.post('./api/queue/items/add', null, {
      params: { expression }
    })
    useNotificationsStore().add({
      text: t('server.appended-tracks', { count: data.count }),
      timeout: 2000,
      type: 'info'
    })
    return await Promise.resolve(data)
  },

  async queue_expression_add_next(expression) {
    const params = {}
    params.expression = expression
    params.position = 0
    const { current } = useQueueStore()
    if (current?.id) {
      params.position = current.position + 1
    }
    const data = await axios.post('./api/queue/items/add', null, { params })
    useNotificationsStore().add({
      text: t('server.appended-tracks', { count: data.count }),
      timeout: 2000,
      type: 'info'
    })
    return await Promise.resolve(data)
  },

  queue_move(itemId, position) {
    return axios.put(`./api/queue/items/${itemId}?new_position=${position}`)
  },

  queue_remove(itemId) {
    return axios.delete(`./api/queue/items/${itemId}`)
  },

  async queue_save_playlist(name) {
    const response = await axios.post('./api/queue/save', null, {
      params: { name }
    })
    useNotificationsStore().add({
      text: t('server.queue-saved', { name }),
      timeout: 2000,
      type: 'info'
    })
    return await Promise.resolve(response)
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
