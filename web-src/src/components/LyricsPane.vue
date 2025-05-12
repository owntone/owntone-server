<template>
  <div class="lyrics is-overlay">
    <div v-for="(verse, index) in visibleLyrics" :key="index">
      <div v-if="verse">
        <div v-if="index === 3" class="title is-5 my-5 lh-2">
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
import { useQueueStore } from '@/stores/queue'

const VISIBLE_VERSES = 7
const MIDDLE_POSITION = Math.floor(VISIBLE_VERSES / 2)

export default {
  name: 'LyricsPane',
  setup() {
    return { playerStore: usePlayerStore(), queueStore: useQueueStore() }
  },
  data() {
    return {
      lastIndex: -1,
      time: 0,
      timerId: null,
      lastProgress: 0,
      lastUpdateTime: 0,
      lyrics: []
    }
  },
  computed: {
    verseIndex() {
      const currentTime = this.time
      const { lyrics } = this
      let start = 0
      let end = lyrics.length - 1
      while (start <= end) {
        const mid = Math.floor((start + end) / 2)
        const midTime = lyrics[mid].time
        const nextTime = lyrics[mid + 1]?.time
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
    visibleLyrics() {
      const current = this.verseIndex
      const total = this.lyrics.length
      return Array.from({ length: VISIBLE_VERSES }, (_, i) => {
        const index = current - MIDDLE_POSITION + i
        return index >= 0 && index < total ? this.lyrics[index] : null
      })
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
      this.lastProgress = progress
      this.lastUpdateTime = Date.now()
      if (!this.playerStore.isPlaying) {
        this.time = progress
      }
    },
    'playerStore.lyricsContent'() {
      this.lyrics = this.parseLyrics()
    },
    verseIndex(index) {
      this.lastIndex = index
    }
  },
  mounted() {
    this.lastProgress = this.playerStore.item_progress_ms
    this.lastUpdateTime = Date.now()
    this.lyrics = this.parseLyrics()
    this.updateTime()
  },
  beforeUnmount() {
    this.stopTimer()
  },
  methods: {
    isWordHighlighted(word) {
      return this.time >= word.start && this.time < word.end
    },
    parseLyrics() {
      const verses = []
      const regex =
        /\[(?<minutes>\d+):(?<seconds>\d+)(?:\.(?<hundredths>\d+))?\] ?(?<text>.*)/u
      this.playerStore.lyricsContent.split('\n').forEach((line) => {
        const match = regex.exec(line)
        if (match) {
          const { text, minutes, seconds, hundredths } = match.groups
          const verse = text.trim()
          if (verse) {
            const time =
              (Number(minutes) * 60 + Number(`${seconds}.${hundredths ?? 0}`)) *
              1000
            verses.push({ text: verse, time })
          }
        }
      })
      verses.forEach((verse, index, lyrics) => {
        const nextTime = lyrics[index + 1]?.time ?? verse.time + 3000
        const totalDuration = nextTime - verse.time
        const words = verse.text.match(/\S+\s*/gu) || []
        const totalLength = words.reduce((sum, word) => sum + word.length, 0)
        let currentTime = verse.time
        verse.words = words.map((text) => {
          const duration = totalDuration * (text.length / totalLength)
          const start = currentTime
          const end = start + duration
          currentTime = end
          return { text, start, end }
        })
      })
      return verses
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
      this.time = this.lastProgress + Date.now() - this.lastUpdateTime
    },
    updateTime() {
      if (this.playerStore.isPlaying) {
        this.startTimer()
      } else {
        this.time = this.lastProgress
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
