import api from '@/api'

const BASE_URL = './api/player'

export default {
  consume(state) {
    return api.put(`${BASE_URL}/consume`, null, { params: { state } })
  },
  next() {
    return api.put(`${BASE_URL}/next`)
  },
  pause() {
    return api.put(`${BASE_URL}/pause`)
  },
  play(params = {}) {
    return api.put(`${BASE_URL}/play`, null, { params })
  },
  previous() {
    return api.put(`${BASE_URL}/previous`)
  },
  repeat(state) {
    return api.put(`${BASE_URL}/repeat`, null, { params: { state } })
  },
  seek(seek_ms) {
    return api.put(`${BASE_URL}/seek`, null, { params: { seek_ms } })
  },
  seekToPosition(position_ms) {
    return api.put(`${BASE_URL}/seek`, null, { params: { position_ms } })
  },
  setVolume(volume, output_id = null) {
    const params = { volume, ...(output_id !== null && { output_id }) }
    return api.put(`${BASE_URL}/volume`, null, { params })
  },
  shuffle(state) {
    return api.put(`${BASE_URL}/shuffle`, null, { params: { state } })
  },
  state() {
    return api.get(BASE_URL)
  },
  stop() {
    return api.put(`${BASE_URL}/stop`)
  }
}
