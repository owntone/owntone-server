<template>
  <div ref="lyrics" class="lyrics is-overlay">
    <template v-for="(verse, index) in visibleLyrics" :key="index">
      <template v-if="verse">
        <div
          v-if="index === 3"
          class="my-5 has-line-height-2 title is-5"
          :class="{ 'is-highlighted': playerStore.isPlaying }"
        >
          <span
            v-for="(word, wordIndex) in verse.words"
            :key="wordIndex"
            class="word"
            :class="{ 'is-word-highlighted': isWordHighlighted(word) }"
            v-text="word.text"
          />
        </div>
        <div v-else class="has-line-height-2" v-text="verse.text" />
      </template>
      <div v-else v-text="'&nbsp;'" />
    </template>
  </div>
</template>

<script>
import { usePlayerStore } from '@/stores/player'
import { useQueueStore } from '@/stores/queue'

export default {
  name: 'LyricsPane',
  setup() {
    return { playerStore: usePlayerStore(), queueStore: useQueueStore() }
  },
  data() {
    return {
      lastIndex: -1,
      lastItemId: -1,
      time: 0,
      timerId: null,
      lastProgressMs: 0,
      lastUpdateTime: 0
    }
  },
  computed: {
    lyrics() {
      const raw = this.playerStore.lyricsContent
      const parsed = []
      if (raw.length > 0) {
        const regex =
          /\[(?<minutes>\d+):(?<seconds>\d+)(?:\.(?<hundredths>\d+))?\] ?(?<text>.*)/u
        raw.split('\n').forEach((line) => {
          const match = regex.exec(line)
          if (match?.groups?.text) {
            const { text, minutes, seconds, hundredths } = match.groups
            const verse = {
              text,
              time: minutes * 60 + Number(`${seconds}.${hundredths ?? 0}`)
            }
            parsed.push(verse)
          }
        })
        parsed.forEach((verse, index, lyrics) => {
          const nextTime = lyrics[index + 1]?.time ?? verse.time + 3
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
      }
      return parsed
    },
    verseIndex() {
      const currentTime = this.time
      const { lyrics } = this
      let start = 0
      let end = lyrics.length - 1
      let index = 0
      while (start <= end) {
        const mid = Math.floor((start + end) / 2)
        const current = lyrics[mid].time
        const next = lyrics[mid + 1]?.time
        if (current <= currentTime && (!next || next > currentTime)) {
          index = mid
          break
        } else if (current < currentTime) {
          start = mid + 1
        } else {
          end = mid - 1
        }
      }
      return index
    },
    visibleLyrics() {
      const VISIBLE_COUNT = 7
      const HALF = Math.floor(VISIBLE_COUNT / 2)
      const current = this.verseIndex
      const total = this.lyrics.length
      return Array.from({ length: VISIBLE_COUNT }, (_, i) => {
        const index = current - HALF + i
        return index >= 0 && index < total ? this.lyrics[index] : null
      })
    }
  },
  watch: {
    verseIndex(newIndex) {
      this.lastIndex = newIndex
    },
    'playerStore.item_progress_ms'(newVal) {
      this.lastProgressMs = newVal
      this.lastUpdateTime = Date.now()
    }
  },
  mounted() {
    this.lastProgressMs = this.playerStore.item_progress_ms
    this.lastUpdateTime = Date.now()
    this.updateTime()
  },
  beforeUnmount() {
    clearTimeout(this.timerId)
  },
  methods: {
    updateTime() {
      const now = Date.now()
      const elapsed = now - this.lastUpdateTime
      if (this.playerStore.isPlaying) {
        this.time = (this.lastProgressMs + elapsed) / 1000
      } else {
        this.time = this.lastProgressMs / 1000
      }
      this.timerId = setTimeout(this.updateTime, 50)
    },
    isWordHighlighted(word) {
      return this.time >= word.start && this.time < word.end
    }
  }
}
</script>

<style scoped>
.lyrics {
  position: absolute;
  top: 0;
  bottom: 0;
  left: 0;
  right: 0;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  justify-content: center;
  align-items: center;
  --mask: linear-gradient(
    180deg,
    transparent 0%,
    rgba(0, 0, 0, 1) 15%,
    rgba(0, 0, 0, 1) 85%,
    transparent 100%
  );
  -webkit-mask: var(--mask);
  mask: var(--mask);
}
.lyrics div.has-line-height-2 {
  line-height: 2rem;
}
.lyrics div {
  line-height: 3rem;
  text-align: center;
}
.word {
  transition: color 0.2s;
}
.is-word-highlighted {
  color: var(--bulma-success);
}
</style>
