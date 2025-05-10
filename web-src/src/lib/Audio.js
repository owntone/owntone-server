export default {
  audio: null,
  context: null,
  source: null,
  play(url) {
    this.stop()
    this.audio = new Audio(`${String(url || '')}?x=${Date.now()}`)
    this.audio.crossOrigin = 'anonymous'
    this.context = new (window.AudioContext || window.webkitAudioContext)()
    this.source = this.context.createMediaElementSource(this.audio)
    this.source.connect(this.context.destination)
    this.audio.addEventListener('canplay', () => {
      this.context.resume().then(() => {
        this.audio.play()
      })
    })
    this.audio.load()
  },
  setVolume(volume) {
    if (this.audio) {
      this.audio.volume = Math.max(0, Math.min(1, Number(volume) || 0))
    }
  },
  stop() {
    try {
      this.audio?.pause()
    } catch (error) {
      // Do nothing
    }
  }
}
