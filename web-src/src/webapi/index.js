import axios from 'axios'
import store from '@/store'

axios.interceptors.response.use(
  function (response) {
    return response
  },
  function (error) {
    if (error.request.status && error.request.responseURL) {
      store.dispatch('add_notification', {
        text:
          'Request failed (status: ' +
          error.request.status +
          ' ' +
          error.request.statusText +
          ', url: ' +
          error.request.responseURL +
          ')',
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

  settings() {
    return axios.get('./api/settings')
  },

  settings_update(categoryName, option) {
    return axios.put(
      './api/settings/' + categoryName + '/' + option.name,
      option
    )
  },

  library_stats() {
    return axios.get('./api/library')
  },

  library_update(scanKind) {
    const params = {}
    if (scanKind) {
      params.scan_kind = scanKind
    }
    return axios.put('./api/update', undefined, { params: params })
  },

  library_rescan(scanKind) {
    const params = {}
    if (scanKind) {
      params.scan_kind = scanKind
    }
    return axios.put('./api/rescan', undefined, { params: params })
  },

  library_count(expression) {
    return axios.get('./api/library/count?expression=' + expression)
  },

  queue() {
    return axios.get('./api/queue')
  },

  queue_clear() {
    return axios.put('./api/queue/clear')
  },

  queue_remove(itemId) {
    return axios.delete('./api/queue/items/' + itemId)
  },

  queue_move(itemId, newPosition) {
    return axios.put(
      './api/queue/items/' + itemId + '?new_position=' + newPosition
    )
  },

  queue_add(uri) {
    return axios.post('./api/queue/items/add?uris=' + uri).then((response) => {
      store.dispatch('add_notification', {
        text: response.data.count + ' tracks appended to queue',
        type: 'info',
        timeout: 2000
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
      .post('./api/queue/items/add?uris=' + uri + '&position=' + position)
      .then((response) => {
        store.dispatch('add_notification', {
          text: response.data.count + ' tracks appended to queue',
          type: 'info',
          timeout: 2000
        })
        return Promise.resolve(response)
      })
  },

  queue_expression_add(expression) {
    const options = {}
    options.expression = expression

    return axios
      .post('./api/queue/items/add', undefined, { params: options })
      .then((response) => {
        store.dispatch('add_notification', {
          text: response.data.count + ' tracks appended to queue',
          type: 'info',
          timeout: 2000
        })
        return Promise.resolve(response)
      })
  },

  queue_expression_add_next(expression) {
    const options = {}
    options.expression = expression
    options.position = 0
    if (store.getters.now_playing && store.getters.now_playing.id) {
      options.position = store.getters.now_playing.position + 1
    }

    return axios
      .post('./api/queue/items/add', undefined, { params: options })
      .then((response) => {
        store.dispatch('add_notification', {
          text: response.data.count + ' tracks appended to queue',
          type: 'info',
          timeout: 2000
        })
        return Promise.resolve(response)
      })
  },

  queue_save_playlist(name) {
    return axios
      .post('./api/queue/save', undefined, { params: { name: name } })
      .then((response) => {
        store.dispatch('add_notification', {
          text: 'Queue saved to playlist "' + name + '"',
          type: 'info',
          timeout: 2000
        })
        return Promise.resolve(response)
      })
  },

  player_status() {
    return axios.get('./api/player')
  },

  player_play_uri(uris, shuffle, position = undefined) {
    const options = {}
    options.uris = uris
    options.shuffle = shuffle ? 'true' : 'false'
    options.clear = 'true'
    options.playback = 'start'
    options.playback_from_position = position

    return axios.post('./api/queue/items/add', undefined, { params: options })
  },

  player_play_expression(expression, shuffle, position = undefined) {
    const options = {}
    options.expression = expression
    options.shuffle = shuffle ? 'true' : 'false'
    options.clear = 'true'
    options.playback = 'start'
    options.playback_from_position = position

    return axios.post('./api/queue/items/add', undefined, { params: options })
  },

  player_play(options = {}) {
    return axios.put('./api/player/play', undefined, { params: options })
  },

  player_playpos(position) {
    return axios.put('./api/player/play?position=' + position)
  },

  player_playid(itemId) {
    return axios.put('./api/player/play?item_id=' + itemId)
  },

  player_pause() {
    return axios.put('./api/player/pause')
  },

  player_stop() {
    return axios.put('./api/player/stop')
  },

  player_next() {
    return axios.put('./api/player/next')
  },

  player_previous() {
    return axios.put('./api/player/previous')
  },

  player_shuffle(newState) {
    const shuffle = newState ? 'true' : 'false'
    return axios.put('./api/player/shuffle?state=' + shuffle)
  },

  player_consume(newState) {
    const consume = newState ? 'true' : 'false'
    return axios.put('./api/player/consume?state=' + consume)
  },

  player_repeat(newRepeatMode) {
    return axios.put('./api/player/repeat?state=' + newRepeatMode)
  },

  player_volume(volume) {
    return axios.put('./api/player/volume?volume=' + volume)
  },

  player_output_volume(outputId, outputVolume) {
    return axios.put(
      './api/player/volume?volume=' + outputVolume + '&output_id=' + outputId
    )
  },

  player_seek_to_pos(newPosition) {
    return axios.put('./api/player/seek?position_ms=' + newPosition)
  },

  player_seek(seekMs) {
    return axios.put('./api/player/seek?seek_ms=' + seekMs)
  },

  outputs() {
    return axios.get('./api/outputs')
  },

  output_update(outputId, output) {
    return axios.put('./api/outputs/' + outputId, output)
  },

  output_toggle(outputId) {
    return axios.put('./api/outputs/' + outputId + '/toggle')
  },

  library_artists(media_kind = undefined) {
    return axios.get('./api/library/artists', {
      params: { media_kind: media_kind }
    })
  },

  library_artist(artistId) {
    return axios.get('./api/library/artists/' + artistId)
  },

  library_artist_albums(artistId) {
    return axios.get('./api/library/artists/' + artistId + '/albums')
  },

  library_albums(media_kind = undefined) {
    return axios.get('./api/library/albums', {
      params: { media_kind: media_kind }
    })
  },

  library_album(albumId) {
    return axios.get('./api/library/albums/' + albumId)
  },

  library_album_tracks(albumId, filter = { limit: -1, offset: 0 }) {
    return axios.get('./api/library/albums/' + albumId + '/tracks', {
      params: filter
    })
  },

  library_album_track_update(albumId, attributes) {
    return axios.put('./api/library/albums/' + albumId + '/tracks', undefined, {
      params: attributes
    })
  },

  library_genres() {
    return axios.get('./api/library/genres')
  },

  library_genre(genre) {
    const genreParams = {
      type: 'albums',
      media_kind: 'music',
      expression: 'genre is "' + genre + '"'
    }
    return axios.get('./api/search', {
      params: genreParams
    })
  },

  library_genre_tracks(genre) {
    const genreParams = {
      type: 'tracks',
      media_kind: 'music',
      expression: 'genre is "' + genre + '"'
    }
    return axios.get('./api/search', {
      params: genreParams
    })
  },

  library_radio_streams() {
    const params = {
      type: 'tracks',
      media_kind: 'music',
      expression: 'data_kind is url and song_length = 0'
    }
    return axios.get('./api/search', {
      params: params
    })
  },

  library_composers() {
    return axios.get('./api/library/composers')
  },

  library_composer(composer) {
    const params = {
      type: 'albums',
      media_kind: 'music',
      expression: 'composer is "' + composer + '"'
    }
    return axios.get('./api/search', {
      params: params
    })
  },

  library_composer_tracks(composer) {
    const params = {
      type: 'tracks',
      media_kind: 'music',
      expression: 'composer is "' + composer + '"'
    }
    return axios.get('./api/search', {
      params: params
    })
  },

  library_artist_tracks(artist) {
    if (artist) {
      const artistParams = {
        type: 'tracks',
        expression: 'songartistid is "' + artist + '"'
      }
      return axios.get('./api/search', {
        params: artistParams
      })
    }
  },

  library_podcasts_new_episodes() {
    const episodesParams = {
      type: 'tracks',
      expression:
        'media_kind is podcast and play_count = 0 ORDER BY time_added DESC'
    }
    return axios.get('./api/search', {
      params: episodesParams
    })
  },

  library_podcast_episodes(albumId) {
    const episodesParams = {
      type: 'tracks',
      expression:
        'media_kind is podcast and songalbumid is "' +
        albumId +
        '" ORDER BY date_released DESC'
    }
    return axios.get('./api/search', {
      params: episodesParams
    })
  },

  library_add(url) {
    return axios.post('./api/library/add', undefined, { params: { url: url } })
  },

  library_playlist_delete(playlistId) {
    return axios.delete('./api/library/playlists/' + playlistId, undefined)
  },

  library_playlists() {
    return axios.get('./api/library/playlists')
  },

  library_playlist_folder(playlistId = 0) {
    return axios.get('./api/library/playlists/' + playlistId + '/playlists')
  },

  library_playlist(playlistId) {
    return axios.get('./api/library/playlists/' + playlistId)
  },

  library_playlist_tracks(playlistId) {
    return axios.get('./api/library/playlists/' + playlistId + '/tracks')
  },

  library_track(trackId) {
    return axios.get('./api/library/tracks/' + trackId)
  },

  library_track_playlists(trackId) {
    return axios.get('./api/library/tracks/' + trackId + '/playlists')
  },

  library_track_update(trackId, attributes = {}) {
    return axios.put('./api/library/tracks/' + trackId, undefined, {
      params: attributes
    })
  },

  library_files(directory = undefined) {
    const filesParams = { directory: directory }
    return axios.get('./api/library/files', {
      params: filesParams
    })
  },

  search(searchParams) {
    return axios.get('./api/search', {
      params: searchParams
    })
  },

  spotify() {
    return axios.get('./api/spotify')
  },

  spotify_login(credentials) {
    return axios.post('./api/spotify-login', credentials)
  },

  spotify_logout() {
    return axios.get('./api/spotify-logout')
  },

  lastfm() {
    return axios.get('./api/lastfm')
  },

  lastfm_login(credentials) {
    return axios.post('./api/lastfm-login', credentials)
  },

  lastfm_logout(credentials) {
    return axios.get('./api/lastfm-logout')
  },

  pairing() {
    return axios.get('./api/pairing')
  },

  pairing_kickoff(pairingReq) {
    return axios.post('./api/pairing', pairingReq)
  },

  artwork_url_append_size_params(artworkUrl, maxwidth = 600, maxheight = 600) {
    if (artworkUrl && artworkUrl.startsWith('/')) {
      if (artworkUrl.includes('?')) {
        return artworkUrl + '&maxwidth=' + maxwidth + '&maxheight=' + maxheight
      }
      return artworkUrl + '?maxwidth=' + maxwidth + '&maxheight=' + maxheight
    }
    return artworkUrl
  }
}
