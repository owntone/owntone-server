<template>
  <div
    class="lyrics is-overlay"
    @wheel.prevent="onScroll"
    @touchmove.prevent="onScroll"
  >
    <div v-for="(verse, index) in visibleVerses" :key="index">
      <div v-if="verse">
        <div v-if="index === MIDDLE_POSITION" class="title is-5 my-5 lh-2">
          <span
            v-for="(word, wordIndex) in verse.words"
            :key="wordIndex"
            :class="{ 'is-highlighted': isWordHighlighted(word) }"
            v-text="word.text"
          />
        </div>
        <div v-else class="lh-2" v-text="verse.text" />
      </div>
      <div v-else v-text="'\u00A0'" />
    </div>
  </div>
</template>

<script>
import { usePlayerStore } from '@/stores/player'

export default {
  name: 'PaneLyrics',
  setup() {
    const VISIBLE_VERSES = 7
    const MIDDLE_POSITION = Math.floor(VISIBLE_VERSES / 2)
    const SCROLL_THRESHOLD = 10
    return {
      MIDDLE_POSITION,
      playerStore: usePlayerStore(),
      SCROLL_THRESHOLD,
      VISIBLE_VERSES
    }
  },
  data() {
    return {
      lastUpdateTime: 0,
      lyrics: { synchronised: false, verses: [] },
      scrollIndex: 0,
      scrollDelta: 0,
      time: 0,
      timerId: null
    }
  },
  computed: {
    verseIndex() {
      const currentTime = this.time
      const { verses } = this.lyrics
      let start = 0
      let end = verses.length - 1
      while (start <= end) {
        const mid = Math.floor((start + end) / 2)
        const midTime = verses[mid].time
        const nextTime = verses[mid + 1]?.time
        if (midTime <= currentTime && (!nextTime || nextTime > currentTime)) {
          return mid
        } else if (midTime < currentTime) {
          start = mid + 1
        } else {
          end = mid - 1
        }
      }
      return -1
    },
    visibleVerses() {
      const { verses, synchronised } = this.lyrics
      const index = synchronised ? this.verseIndex : this.scrollIndex
      return Array.from(
        { length: this.VISIBLE_VERSES },
        (_, i) => verses[index - this.MIDDLE_POSITION + i] ?? null
      )
    }
  },
  watch: {
    'playerStore.isPlaying'(isPlaying) {
      if (isPlaying) {
        this.lastUpdateTime = Date.now()
        this.startTimer()
      } else {
        this.stopTimer()
      }
    },
    'playerStore.item_progress_ms'(progress) {
      this.lastUpdateTime = Date.now()
      if (!this.playerStore.isPlaying) {
        this.time = progress
      }
    },
    'playerStore.lyricsContent'() {
      this.lyrics = this.parseLyrics()
    }
  },
  mounted() {
    this.playerStore.initialise()
    this.lastUpdateTime = Date.now()
    this.lyrics = this.parseLyrics()
    this.updateTime()
  },
  beforeUnmount() {
    this.stopTimer()
  },
  methods: {
    onScroll(event) {
      if (this.verseIndex >= 0) {
        return
      }
      this.scrollDelta += event.deltaY
      if (Math.abs(this.scrollDelta) >= this.SCROLL_THRESHOLD) {
        const newIndex = this.scrollIndex + Math.sign(this.scrollDelta)
        if (newIndex >= -1 && newIndex < this.lyrics.verses.length) {
          this.scrollIndex = newIndex
        }
        this.scrollDelta = 0
      }
    },
    isWordHighlighted(word) {
      return this.time >= word.start && this.time < word.end
    },
    parseLyrics() {
      const lyrics = { synchronised: false, verses: [] }
      const regex =
        /(?:\[(?<minutes>\d+):(?<seconds>\d+)(?:\.(?<hundredths>\d+))?\])?\s*(?<text>\S.*\S)?\s*/u
      this.playerStore.lyricsContent.split('\n').forEach((line) => {
        const match = regex.exec(line)
        if (match) {
          const { text, minutes, seconds, hundredths } = match.groups
          const verse = text
          if (verse) {
            const time =
              (Number(minutes) * 60 + Number(`${seconds}.${hundredths ?? 0}`)) *
              1000
            lyrics.synchronised = !isNaN(time)
            lyrics.verses.push({ text: verse, time })
          }
        }
      })
      lyrics.verses.forEach((verse, index, verses) => {
        const nextTime = verses[index + 1]?.time ?? verse.time + 3000
        const totalDuration = nextTime - verse.time
        const words = verse.text.match(/\S+\s*/gu) || []
        const totalLength = words.reduce((sum, word) => sum + word.length, 0)
        let currentTime = verse.time
        verse.words = words.map((text) => {
          const start = currentTime
          const end = start + totalDuration * (text.length / totalLength)
          currentTime = end
          return { text, start, end }
        })
      })
      return lyrics
    },
    startTimer() {
      if (this.timerId) {
        return
      }
      this.timerId = setInterval(this.tick, 100)
    },
    stopTimer() {
      if (this.timerId) {
        clearInterval(this.timerId)
        this.timerId = null
      }
    },
    tick() {
      this.time =
        this.playerStore.item_progress_ms + Date.now() - this.lastUpdateTime
    },
    updateTime() {
      this.lastUpdateTime = Date.now()
      if (this.playerStore.isPlaying) {
        this.startTimer()
      } else {
        this.time = this.playerStore.item_progress_ms
      }
    }
  }
}
</script>

<style scoped>
.lyrics {
  position: absolute;
  inset: 0;
  display: flex;
  flex-direction: column;
  justify-content: center;
  align-items: center;
  --mask: linear-gradient(
    180deg,
    transparent 0%,
    black 15%,
    black 85%,
    transparent 100%
  );
  -webkit-mask: var(--mask);
  mask: var(--mask);
}
.lyrics div.lh-2 {
  line-height: 2rem;
}
.is-highlighted {
  color: var(--bulma-success);
  transition: color 0.2s;
}
</style>
