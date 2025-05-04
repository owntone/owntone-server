<template>
  <div
    ref="lyrics"
    class="lyrics is-overlay"
    @touchstart="autoScrolling = false"
    @touchend="autoScrolling = true"
    @scroll.passive="startScrolling"
    @wheel.passive="startScrolling"
  >
    <template v-for="(verse, index) in lyrics" :key="index">
      <div
        v-if="index === verseIndex"
        :class="{ 'is-highlighted': playerStore.isPlaying }"
        class="has-line-height-2 my-5 title is-5"
      >
        <span
          v-for="word in verse.words"
          :key="word"
          :style="{ 'animation-duration': `${word.delay}s` }"
          v-text="word.text"
        />
      </div>
      <div v-else>
        {{ verse.text }}
      </div>
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
    /*
     * Non reactive. Used as a cache to speed up the finding of lyrics
     * index in the array for the current time.
     */
    this.lastIndex = -1
    // Fired upon scrolling, thus disabling the auto scrolling for 5 seconds
    this.scrollingTimer = null
    this.lastItemId = -1
    return {
      autoScrolling: true
    }
  },
  computed: {
    lyrics() {
      const raw = this.playerStore.lyricsContent
      const parsed = []
      if (raw.length > 0) {
        // Parse the lyrics
        const regex =
          /\[(?<minutes>\d+):(?<seconds>\d+)(?:\.(?<hundredths>\d+))?\] ?(?<text>.*)/u
        raw.split('\n').forEach((line) => {
          const { text, minutes, seconds, hundredths } = regex.exec(line).groups
          if (text) {
            const verse = {
              text,
              time:
                minutes * 60 + Number(seconds) + Number(`.${hundredths || 0}`)
            }
            parsed.push(verse)
          }
        })
        // Split the verses into words
        parsed.forEach((verse, index, lyrics) => {
          const unitDuration =
            ((lyrics[index + 1]?.time ?? verse.time + 3) - verse.time) /
            verse.text.length
          let delay = 0
          verse.words = verse.text.match(/\S+\s*/gu).map((text) => {
            const duration = text.length * unitDuration
            delay += duration
            return { delay, duration, text }
          })
        })
      }
      return parsed
    },
    verseIndex() {
      if (this.lyrics.length && this.lyrics[0].time) {
        const currentTime = this.playerStore.item_progress_ms / 1000,
          { lyrics } = this,
          { length } = lyrics,
          trackChanged = this.playerStore.item_id !== this.lastItemId,
          trackSeeked =
            this.lastIndex >= 0 &&
            this.lastIndex < length &&
            lyrics[this.lastIndex].time > currentTime
        // Reset the cache when the track has changed or has been seeked
        if (trackChanged || trackSeeked) {
          this.resetScrolling()
        }
        // Check the next two items and the last one before searching
        if (
          (this.lastIndex < length - 1 &&
            lyrics[this.lastIndex + 1].time > currentTime) ||
          this.lastIndex === length - 1
        ) {
          return this.lastIndex
        }
        if (
          this.lastIndex < length - 2 &&
          lyrics[this.lastIndex + 2].time > currentTime
        ) {
          return this.lastIndex + 1
        }
        // Not found, then start a binary search
        let end = length - 1,
          index = -1,
          start = 0
        while (start <= end) {
          index = (start + end) >> 1
          const currentVerseTime = lyrics[index].time
          const nextVerseTime = lyrics[index + 1]?.time
          if (
            currentVerseTime <= currentTime &&
            (nextVerseTime > currentTime || !nextVerseTime)
          ) {
            break
          }
          if (currentVerseTime < currentTime) {
            start = index + 1
          } else {
            end = index - 1
          }
        }
        return index
      }
      this.resetScrolling()
      return -1
    }
  },
  watch: {
    verseIndex() {
      if (this.autoScrolling) {
        this.scrollToVerse()
      }
      this.lastIndex = this.verseIndex
    }
  },
  methods: {
    resetScrolling() {
      // Scroll to the start of the lyrics in all cases
      if (this.playerStore.item_id !== this.lastItemId && this.$refs.lyrics) {
        this.$refs.lyrics.scrollTo(0, 0)
      }
      this.lastItemId = this.playerStore.item_id
      this.lastIndex = -1
    },
    scrollToVerse() {
      const pane = this.$refs.lyrics
      if (this.verseIndex === -1) {
        pane.scrollTo(0, 0)
        return
      }
      const currentVerse = pane.children[this.verseIndex]
      pane.scrollBy({
        behavior: 'smooth',
        top:
          currentVerse.offsetTop -
          pane.offsetHeight / 2 +
          currentVerse.offsetHeight / 2 -
          pane.scrollTop
      })
    },
    startScrolling(event) {
      // Consider only user events
      if (event.screenX ?? event.screenY) {
        this.autoScrolling = false
        clearTimeout(this.scrollingTimer)
        // Reenable automatic scrolling after 2 seconds
        this.scrollingTimer = setTimeout((this.autoScrolling = true), 2000)
      }
    }
  }
}
</script>

<style scoped>
.lyrics {
  position: absolute;
  overflow: auto;
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
.lyrics div.is-highlighted span {
  animation: pop-color 0s linear forwards;
}
.lyrics div.has-line-height-2 {
  line-height: 2rem;
}
.lyrics div {
  line-height: 3rem;
}
.lyrics div:first-child {
  padding-top: calc(25vh - 2rem);
}
.lyrics div:last-child {
  padding-bottom: calc(25vh - 3rem);
}
@keyframes pop-color {
  0% {
    color: var(--bulma-black);
  }
  100% {
    color: var(--bulma-success);
  }
}
</style>
