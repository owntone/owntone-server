/**
 * Audio handler object
 * Inspired by https://github.com/rainner/soma-fm-player
 * (released under MIT licence)
 */
export default {
  _audio: new Audio(),
  _context: null,

  // Setup audio routing
  setupAudio() {
    this._context = new (window.AudioContext || window.webkitAudioContext)()
    const source = this._context.createMediaElementSource(this._audio)
    source.connect(this._context.destination)
    this._audio.addEventListener('canplaythrough', (e) => {
      this._audio.play()
    })
    this._audio.addEventListener('canplay', (e) => {
      this._audio.play()
    })
    return this._audio
  },

  // Set audio volume
  setVolume(volume) {
    if (this._audio) {
      this._audio.volume = Math.min(1, Math.max(0, parseFloat(volume) || 0.0))
    }
  },

  // Play audio source url
  playSource(source) {
    this.stopAudio()
    this._context.resume().then(() => {
      this._audio.src = `${String(source || '')}?x=${Date.now()}`
      this._audio.crossOrigin = 'anonymous'
      this._audio.load()
    })
  },

  // Stop playing audio
  stopAudio() {
    try {
      this._audio.pause()
    } catch (e) {
      // Continue regardless of error
    }
    try {
      this._audio.stop()
    } catch (e) {
      // Continue regardless of error
    }
    try {
      this._audio.close()
    } catch (e) {
      // Continue regardless of error
    }
  }
}
