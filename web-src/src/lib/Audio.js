export default {
  audio: null,
  play(url) {
    this.audio = new Audio(`${String(url || '')}?x=${Date.now()}`)
    this.audio.crossOrigin = 'anonymous'
    const context = new (window.AudioContext || window.webkitAudioContext)()
    const source = context.createMediaElementSource(this.audio)
    source.connect(context.destination)
    this.audio.addEventListener('canplay', () => {
      context.resume().then(() => {
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
      //Do nothing
    }
  }
}
