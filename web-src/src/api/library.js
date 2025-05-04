import api from '@/api'

export default {
  add(url) {
    return api.post('./api/library/add', null, { params: { url } })
  },
  album(id) {
    return api.get(`./api/library/albums/${id}`)
  },
  albumTracks(id, filter = { limit: -1, offset: 0 }) {
    return api.get(`./api/library/albums/${id}/tracks`, {
      params: filter
    })
  },
  albums(media_kind) {
    return api.get('./api/library/albums', { params: { media_kind } })
  },
  artist(id) {
    return api.get(`./api/library/artists/${id}`)
  },
  artistAlbums(id) {
    return api.get(`./api/library/artists/${id}/albums`)
  },
  async artistTracks(artist) {
    const params = {
      expression: `songartistid is "${artist}"`,
      type: 'tracks'
    }
    const data = await api.get('./api/search', { params })
    return data.tracks
  },
  artists(media_kind) {
    return api.get('./api/library/artists', { params: { media_kind } })
  },
  composer(composer) {
    return api.get(`./api/library/composers/${encodeURIComponent(composer)}`)
  },
  async composerAlbums(composer) {
    const params = {
      expression: `composer is "${composer}" and media_kind is music`,
      type: 'albums'
    }
    const data = await api.get('./api/search', { params })
    return data.albums
  },
  async composerTracks(composer) {
    const params = {
      expression: `composer is "${composer}" and media_kind is music`,
      type: 'tracks'
    }
    const data = await api.get('./api/search', { params })
    return data.tracks
  },
  composers(media_kind) {
    return api.get('./api/library/composers', { params: { media_kind } })
  },
  files(directory) {
    return api.get('./api/library/files', { params: { directory } })
  },
  async genre(genre, mediaKind) {
    const params = {
      expression: `genre is "${genre}" and media_kind is ${mediaKind}`,
      type: 'genres'
    }
    const data = await api.get('./api/search', { params })
    return data.genres
  },
  async genreAlbums(genre, mediaKind) {
    const params = {
      expression: `genre is "${genre}" and media_kind is ${mediaKind}`,
      type: 'albums'
    }
    const data = await api.get('./api/search', { params })
    return data.albums
  },
  async genreTracks(genre, mediaKind) {
    const params = {
      expression: `genre is "${genre}" and media_kind is ${mediaKind}`,
      type: 'tracks'
    }
    const data = await api.get('./api/search', { params })
    return data.tracks
  },
  async genres(mediaKind) {
    const params = {
      expression: `media_kind is ${mediaKind}`,
      type: 'genres'
    }
    const data = await api.get('./api/search', { params })
    return data.genres
  },
  async newPodcastEpisodes() {
    const params = {
      expression:
        'media_kind is podcast and play_count = 0 ORDER BY time_added DESC',
      type: 'tracks'
    }
    const data = await api.get('./api/search', { params })
    return data.tracks
  },
  playlist(id) {
    return api.get(`./api/library/playlists/${id}`)
  },
  playlistDelete(id) {
    return api.delete(`./api/library/playlists/${id}`)
  },
  playlistFolder(id = 0) {
    return api.get(`./api/library/playlists/${id}/playlists`)
  },
  playlistTracks(id) {
    return api.get(`./api/library/playlists/${id}/tracks`)
  },
  async podcastEpisodes(id) {
    const params = {
      expression: `media_kind is podcast and songalbumid is "${id}" ORDER BY date_released DESC`,
      type: 'tracks'
    }
    const data = await api.get('./api/search', { params })
    return data.tracks
  },
  async radioStreams() {
    const params = {
      expression: 'data_kind is url and song_length = 0',
      media_kind: 'music',
      type: 'tracks'
    }
    const data = await api.get('./api/search', { params })
    return data.tracks
  },
  rescan(scan_kind) {
    return api.put('./api/rescan', null, { params: { scan_kind } })
  },
  rssCount() {
    return api.get('./api/library/count', {
      params: { expression: 'scan_kind is rss' }
    })
  },
  search(params) {
    return api.get('./api/search', { params })
  },
  state() {
    return api.get('./api/library')
  },
  track(id) {
    return api.get(`./api/library/tracks/${id}`)
  },
  trackPlaylists(id) {
    return api.get(`./api/library/tracks/${id}/playlists`)
  },
  update(scan_kind) {
    return api.put('./api/update', null, { params: { scan_kind } })
  },
  updateAlbum(id, attributes) {
    return api.put(`./api/library/albums/${id}/tracks`, null, {
      params: attributes
    })
  },
  updateTrack(id, attributes = {}) {
    return api.put(`./api/library/tracks/${id}`, null, {
      params: attributes
    })
  }
}
