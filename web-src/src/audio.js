/**
 * Audio handler object
 * Taken from https://github.com/rainner/soma-fm-player (released under MIT licence)
 */
export default {
  _audio: new Audio(),
  _context: null,
  _source: null,
  _gain: null,

  // setup audio routing
  setupAudio() {
    const AudioContext = window.AudioContext || window.webkitAudioContext
    this._context = new AudioContext()
    this._source = this._context.createMediaElementSource(this._audio)
    this._gain = this._context.createGain()

    this._source.connect(this._gain)
    this._gain.connect(this._context.destination)

    this._audio.addEventListener('canplaythrough', (e) => {
      this._audio.play()
    })
    this._audio.addEventListener('canplay', (e) => {
      this._audio.play()
    })
    return this._audio
  },

  // set audio volume
  setVolume(volume) {
    if (!this._gain) return
    volume = parseFloat(volume) || 0.0
    volume = volume < 0 ? 0 : volume
    volume = volume > 1 ? 1 : volume
    this._gain.gain.value = volume
  },

  // play audio source url
  playSource(source) {
    this.stopAudio()
    this._context.resume().then(() => {
      this._audio.src = String(source || '') + '?x=' + Date.now()
      this._audio.crossOrigin = 'anonymous'
      this._audio.load()
    })
  },

  // stop playing audio
  stopAudio() {
    try {
      this._audio.pause()
    } catch (e) {}
    try {
      this._audio.stop()
    } catch (e) {}
    try {
      this._audio.close()
    } catch (e) {}
  }
}
