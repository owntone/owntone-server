import axios from 'axios'
import store from '@/store'

axios.interceptors.response.use(function (response) {
  return response
}, function (error) {
  store.dispatch('add_notification', { text: 'Request failed (status: ' + error.request.status + ' ' + error.request.statusText + ', url: ' + error.request.responseURL + ')', type: 'danger' })
  return Promise.reject(error)
})

export default {
  config () {
    return axios.get('/api/config')
  },

  library_stats () {
    return axios.get('/api/library')
  },

  library_update () {
    return axios.get('/api/update')
  },

  library_count (expression) {
    return axios.get('/api/library/count?expression=' + expression)
  },

  queue () {
    return axios.get('/api/queue')
  },

  queue_clear () {
    return axios.put('/api/queue/clear')
  },

  queue_remove (itemId) {
    return axios.delete('/api/queue/items/' + itemId)
  },

  queue_move (itemId, newPosition) {
    return axios.put('/api/queue/items/' + itemId + '?new_position=' + newPosition)
  },

  queue_add (uri) {
    return axios.post('/api/queue/items/add?uris=' + uri).then((response) => {
      store.dispatch('add_notification', { text: response.data.count + ' tracks appended to queue', type: 'info', timeout: 2000 })
      return Promise.resolve(response)
    })
  },

  queue_add_next (uri) {
    var position = 0
    if (store.getters.now_playing && store.getters.now_playing.id) {
      position = store.getters.now_playing.position + 1
    }
    return axios.post('/api/queue/items/add?uris=' + uri + '&position=' + position).then((response) => {
      store.dispatch('add_notification', { text: response.data.count + ' tracks appended to queue', type: 'info', timeout: 2000 })
      return Promise.resolve(response)
    })
  },

  queue_expression_add (expression) {
    var options = {}
    options.expression = expression

    return axios.post('/api/queue/items/add', undefined, { params: options }).then((response) => {
      store.dispatch('add_notification', { text: response.data.count + ' tracks appended to queue', type: 'info', timeout: 2000 })
      return Promise.resolve(response)
    })
  },

  queue_expression_add_next (expression) {
    var options = {}
    options.expression = expression
    options.position = 0
    if (store.getters.now_playing && store.getters.now_playing.id) {
      options.position = store.getters.now_playing.position + 1
    }

    return axios.post('/api/queue/items/add', undefined, { params: options }).then((response) => {
      store.dispatch('add_notification', { text: response.data.count + ' tracks appended to queue', type: 'info', timeout: 2000 })
      return Promise.resolve(response)
    })
  },

  queue_save_playlist (name) {
    return axios.post('/api/queue/save', undefined, { params: { 'name': name } }).then((response) => {
      store.dispatch('add_notification', { text: 'Queue saved to playlist "' + name + '"', type: 'info', timeout: 2000 })
      return Promise.resolve(response)
    })
  },

  player_status () {
    return axios.get('/api/player')
  },

  player_play_uri (uris, shuffle, position = undefined) {
    var options = {}
    options.uris = uris
    options.shuffle = shuffle ? 'true' : 'false'
    options.clear = 'true'
    options.playback = 'start'
    options.playback_from_position = position

    return axios.post('/api/queue/items/add', undefined, { params: options })
  },

  player_play_expression (expression, shuffle, position = undefined) {
    var options = {}
    options.expression = expression
    options.shuffle = shuffle ? 'true' : 'false'
    options.clear = 'true'
    options.playback = 'start'
    options.playback_from_position = position

    return axios.post('/api/queue/items/add', undefined, { params: options })
  },

  player_play (options = {}) {
    return axios.put('/api/player/play', undefined, { params: options })
  },

  player_playpos (position) {
    return axios.put('/api/player/play?position=' + position)
  },

  player_playid (itemId) {
    return axios.put('/api/player/play?item_id=' + itemId)
  },

  player_pause () {
    return axios.put('/api/player/pause')
  },

  player_stop () {
    return axios.put('/api/player/stop')
  },

  player_next () {
    return axios.put('/api/player/next')
  },

  player_previous () {
    return axios.put('/api/player/previous')
  },

  player_shuffle (newState) {
    var shuffle = newState ? 'true' : 'false'
    return axios.put('/api/player/shuffle?state=' + shuffle)
  },

  player_consume (newState) {
    var consume = newState ? 'true' : 'false'
    return axios.put('/api/player/consume?state=' + consume)
  },

  player_repeat (newRepeatMode) {
    return axios.put('/api/player/repeat?state=' + newRepeatMode)
  },

  player_volume (volume) {
    return axios.put('/api/player/volume?volume=' + volume)
  },

  player_output_volume (outputId, outputVolume) {
    return axios.put('/api/player/volume?volume=' + outputVolume + '&output_id=' + outputId)
  },

  player_seek (newPosition) {
    return axios.put('/api/player/seek?position_ms=' + newPosition)
  },

  outputs () {
    return axios.get('/api/outputs')
  },

  output_update (outputId, output) {
    return axios.put('/api/outputs/' + outputId, output)
  },

  library_artists () {
    return axios.get('/api/library/artists?media_kind=music')
  },

  library_artist (artistId) {
    return axios.get('/api/library/artists/' + artistId)
  },

  library_albums (artistId) {
    if (artistId) {
      return axios.get('/api/library/artists/' + artistId + '/albums')
    }
    return axios.get('/api/library/albums?media_kind=music')
  },

  library_album (albumId) {
    return axios.get('/api/library/albums/' + albumId)
  },

  library_album_tracks (albumId) {
    return axios.get('/api/library/albums/' + albumId + '/tracks')
  },

  library_genres () {
    return axios.get('/api/library/genres')
  },

  library_genre (genre) {
    var genreParams = {
      'type': 'albums',
      'media_kind': 'music',
      'expression': 'genre is "' + genre + '"'
    }
    return axios.get('/api/search', {
      params: genreParams
    })
  },

  library_genre_tracks (genre) {
    var genreParams = {
      'type': 'tracks',
      'media_kind': 'music',
      'expression': 'genre is "' + genre + '"'
    }
    return axios.get('/api/search', {
      params: genreParams
    })
  },

  library_artist_tracks (artist) {
    if (artist) {
      var artistParams = {
        'type': 'tracks',
        'expression': 'songartistid is "' + artist + '"'
      }
      return axios.get('/api/search', {
        params: artistParams
      })
    }
  },

  library_podcasts () {
    return axios.get('/api/library/albums?media_kind=podcast')
  },

  library_podcasts_new_episodes () {
    var episodesParams = {
      'type': 'tracks',
      'expression': 'media_kind is podcast and play_count = 0 ORDER BY time_added DESC'
    }
    return axios.get('/api/search', {
      params: episodesParams
    })
  },

  library_podcast_episodes (albumId) {
    var episodesParams = {
      'type': 'tracks',
      'expression': 'media_kind is podcast and songalbumid is "' + albumId + '" ORDER BY time_added DESC'
    }
    return axios.get('/api/search', {
      params: episodesParams
    })
  },

  library_audiobooks () {
    return axios.get('/api/library/albums?media_kind=audiobook')
  },

  library_playlists () {
    return axios.get('/api/library/playlists')
  },

  library_playlist (playlistId) {
    return axios.get('/api/library/playlists/' + playlistId)
  },

  library_playlist_tracks (playlistId) {
    return axios.get('/api/library/playlists/' + playlistId + '/tracks')
  },

  library_track (trackId) {
    return axios.get('/api/library/tracks/' + trackId)
  },

  library_track_update (trackId, attributes = {}) {
    return axios.put('/api/library/tracks/' + trackId, undefined, { params: attributes })
  },

  library_track_set_rating (trackId, rating) {
    return axios.put('/api/library/tracks/' + trackId, undefined, { params: { 'rating': rating } }).then((response) => {
      store.dispatch('add_notification', { text: 'track rating updated', type: 'info', timeout: 1500 })
      return Promise.resolve(response)
    })
  },

  library_files (directory = undefined) {
    var filesParams = { 'directory': directory }
    return axios.get('/api/library/files', {
      params: filesParams
    })
  },

  search (searchParams) {
    return axios.get('/api/search', {
      params: searchParams
    })
  },

  spotify () {
    return axios.get('/api/spotify')
  }
}
