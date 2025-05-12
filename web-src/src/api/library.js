import api from '@/api'

const BASE_URL = './api/library'
const SEARCH_URL = './api/search'

export default {
  add(url) {
    return api.post(`${BASE_URL}/add`, null, { params: { url } })
  },
  album(id) {
    return api.get(`${BASE_URL}/albums/${id}`)
  },
  albumTracks(id, filter = { limit: -1, offset: 0 }) {
    return api.get(`${BASE_URL}/albums/${id}/tracks`, { params: filter })
  },
  albums(media_kind) {
    return api.get(`${BASE_URL}/albums`, { params: { media_kind } })
  },
  artist(id) {
    return api.get(`${BASE_URL}/artists/${id}`)
  },
  artistAlbums(id) {
    return api.get(`${BASE_URL}/artists/${id}/albums`)
  },
  async artistTracks(artist) {
    const params = {
      expression: `songartistid is "${artist}"`,
      type: 'tracks'
    }
    return (await api.get(SEARCH_URL, { params })).tracks
  },
  artists(media_kind) {
    return api.get(`${BASE_URL}/artists`, { params: { media_kind } })
  },
  composer(composer) {
    return api.get(`${BASE_URL}/composers/${encodeURIComponent(composer)}`)
  },
  async composerAlbums(composer) {
    const params = {
      expression: `composer is "${composer}" and media_kind is music`,
      type: 'albums'
    }
    return (await api.get(SEARCH_URL, { params })).albums
  },
  async composerTracks(composer) {
    const params = {
      expression: `composer is "${composer}" and media_kind is music`,
      type: 'tracks'
    }
    return (await api.get(SEARCH_URL, { params })).tracks
  },
  composers(media_kind) {
    return api.get(`${BASE_URL}/composers`, { params: { media_kind } })
  },
  files(directory) {
    return api.get(`${BASE_URL}/files`, { params: { directory } })
  },
  async genre(genre, mediaKind) {
    const params = {
      expression: `genre is "${genre}" and media_kind is ${mediaKind}`,
      type: 'genres'
    }
    return (await api.get(SEARCH_URL, { params })).genres
  },
  async genreAlbums(genre, mediaKind) {
    const params = {
      expression: `genre is "${genre}" and media_kind is ${mediaKind}`,
      type: 'albums'
    }
    return (await api.get(SEARCH_URL, { params })).albums
  },
  async genreTracks(genre, mediaKind) {
    const params = {
      expression: `genre is "${genre}" and media_kind is ${mediaKind}`,
      type: 'tracks'
    }
    return (await api.get(SEARCH_URL, { params })).tracks
  },
  async genres(mediaKind) {
    const params = {
      expression: `media_kind is ${mediaKind}`,
      type: 'genres'
    }
    return (await api.get(SEARCH_URL, { params })).genres
  },
  async newPodcastEpisodes() {
    const params = {
      expression:
        'media_kind is podcast and play_count = 0 ORDER BY time_added DESC',
      type: 'tracks'
    }
    return (await api.get(SEARCH_URL, { params })).tracks
  },
  playlist(id) {
    return api.get(`${BASE_URL}/playlists/${id}`)
  },
  playlistDelete(id) {
    return api.delete(`${BASE_URL}/playlists/${id}`)
  },
  playlistFolder(id = 0) {
    return api.get(`${BASE_URL}/playlists/${id}/playlists`)
  },
  playlistTracks(id) {
    return api.get(`${BASE_URL}/playlists/${id}/tracks`)
  },
  async podcastEpisodes(id) {
    const params = {
      expression: `media_kind is podcast and songalbumid is "${id}" ORDER BY date_released DESC`,
      type: 'tracks'
    }
    return (await api.get(SEARCH_URL, { params })).tracks
  },
  async radioStreams() {
    const params = {
      expression: 'data_kind is url and song_length = 0',
      media_kind: 'music',
      type: 'tracks'
    }
    return (await api.get(SEARCH_URL, { params })).tracks
  },
  rescan(scan_kind) {
    return api.put('./api/rescan', null, { params: { scan_kind } })
  },
  rssCount() {
    return api.get(`${BASE_URL}/count`, {
      params: { expression: 'scan_kind is rss' }
    })
  },
  search(params) {
    return api.get(SEARCH_URL, { params })
  },
  state() {
    return api.get(BASE_URL)
  },
  track(id) {
    return api.get(`${BASE_URL}/tracks/${id}`)
  },
  trackPlaylists(id) {
    return api.get(`${BASE_URL}/tracks/${id}/playlists`)
  },
  update(scan_kind) {
    return api.put('./api/update', null, { params: { scan_kind } })
  },
  updateAlbum(id, attributes) {
    return api.put(`${BASE_URL}/albums/${id}/tracks`, null, {
      params: attributes
    })
  },
  updateTrack(id, attributes = {}) {
    return api.put(`${BASE_URL}/tracks/${id}`, null, { params: attributes })
  }
}
