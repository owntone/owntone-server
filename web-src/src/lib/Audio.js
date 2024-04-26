/**
 * Audio handler object
 * Inspired by https://github.com/rainner/soma-fm-player
 * (released under MIT licence)
 */
export default {
  audio: new Audio(),
  context: null,

  // Play audio source url
  play(source) {
    this.stop()
    this.context.resume().then(() => {
      this.audio.src = `${String(source || '')}?x=${Date.now()}`
      this.audio.crossOrigin = 'anonymous'
      this.audio.load()
    })
  },

  // Set audio volume
  setVolume(volume) {
    if (this.audio) {
      this.audio.volume = Math.min(1, Math.max(0, parseFloat(volume) || 0.0))
    }
  },

  // Setup audio routing
  setup() {
    this.context = new (window.AudioContext || window.webkitAudioContext)()
    const source = this.context.createMediaElementSource(this.audio)
    source.connect(this.context.destination)
    this.audio.addEventListener('canplaythrough', (event) => {
      this.audio.play()
    })
    this.audio.addEventListener('canplay', (event) => {
      this.audio.play()
    })
    return this.audio
  },

  // Stop playing audio
  stop() {
    try {
      this.audio.pause()
    } catch (error) {
      // Continue regardless of error
    }
    try {
      this.audio.stop()
    } catch (error) {
      // Continue regardless of error
    }
    try {
      this.audio.close()
    } catch (error) {
      // Continue regardless of error
    }
  }
}
