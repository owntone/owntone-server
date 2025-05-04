import api from '@/api'

export default {
  consume(state) {
    return api.put(`./api/player/consume?state=${state}`)
  },
  next() {
    return api.put('./api/player/next')
  },
  outputVolume(outputId, outputVolume) {
    return api.put(
      `./api/player/volume?volume=${outputVolume}&output_id=${outputId}`
    )
  },
  pause() {
    return api.put('./api/player/pause')
  },
  play(params = {}) {
    return api.put('./api/player/play', null, { params })
  },
  previous() {
    return api.put('./api/player/previous')
  },
  repeat(mode) {
    return api.put(`./api/player/repeat?state=${mode}`)
  },
  seek(seekMs) {
    return api.put(`./api/player/seek?seek_ms=${seekMs}`)
  },
  seekToPosition(position) {
    return api.put(`./api/player/seek?position_ms=${position}`)
  },
  shuffle(state) {
    return api.put(`./api/player/shuffle?state=${state}`)
  },
  state() {
    return api.get('./api/player')
  },
  stop() {
    return api.put('./api/player/stop')
  },
  volume(volume) {
    return api.put(`./api/player/volume?volume=${volume}`)
  }
}
