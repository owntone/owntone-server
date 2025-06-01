<template>
  <div
    class="is-overlay"
    :class="{
      'is-synchronised': lyrics.synchronised,
      'is-unsynchronised': !lyrics.synchronised
    }"
  >
    <template v-for="(verse, index) in visibleVerses" :key="index">
      <div v-if="isVerseHighlighted(index)" class="title is-5 my-5">
        <span
          v-for="(word, wordIndex) in verse.words"
          :key="wordIndex"
          :class="{ 'is-highlighted': isWordHighlighted(word) }"
          v-text="word.text"
        />
      </div>
      <div v-else class="verse" v-text="verse.text" />
    </template>
  </div>
</template>

<script>
import { usePlayerStore } from '@/stores/player'

export default {
  name: 'PaneLyrics',
  setup() {
    const VISIBLE_VERSES = 7
    const MIDDLE_POSITION = Math.floor(VISIBLE_VERSES / 2)
    return {
      MIDDLE_POSITION,
      VISIBLE_VERSES,
      playerStore: usePlayerStore()
    }
  },
  data() {
    return {
      lastUpdateTime: 0,
      lyrics: { synchronised: false, verses: [] },
      time: 0,
      timerId: null
    }
  },
  computed: {
    verseIndex(time, verses) {
      let low = 0
      let high = verses.length - 1
      while (low <= high) {
        const mid = Math.floor((low + high) / 2),
          midTime = verses[mid].time,
          nextTime = verses[mid + 1]?.time
        if (midTime <= time && (!nextTime || nextTime > time)) {
          return mid
        } else if (midTime < time) {
          low = mid + 1
        } else {
          high = mid - 1
        }
      }
      return -1
    },
    visibleVerses() {
      const { verses, synchronised } = this.lyrics
      let start = 0
      let { length } = verses
      if (synchronised) {
        start = this.verseIndex(this.time, verses) - this.MIDDLE_POSITION
        length = this.VISIBLE_VERSES
      }
      return Array.from(
        { length },
        (_, i) => verses[start + i] ?? { text: '\u00A0' }
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
    isVerseHighlighted(index) {
      return index === this.MIDDLE_POSITION && this.lyrics.synchronised
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
          return { end, start, text }
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

<style lang="scss" scoped>
.overlay {
  display: flex;
  flex-direction: column;
  inset: 0;
  position: absolute;
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
.is-synchronised {
  @extend .overlay;
  justify-content: center;
}
.is-unsynchronised {
  @extend .overlay;
  overflow-y: scroll;
}
.verse {
  line-height: 2.5rem;
  &:first-child {
    margin-top: calc(50% - 1.25rem);
  }
  &:last-child {
    margin-bottom: calc(50% - 1.25rem);
  }
}
.is-highlighted {
  color: var(--bulma-success);
  transition: color 0.2s;
}
</style>
