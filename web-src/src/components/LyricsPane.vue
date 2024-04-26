<template>
  <div
    ref="lyrics"
    class="lyrics"
    @touchstart="autoScrolling = false"
    @touchend="autoScrolling = true"
    @scroll.passive="start_scrolling"
    @wheel.passive="start_scrolling"
  >
    <template v-for="(verse, index) in lyrics" :key="index">
      <div
        v-if="index === verse_index"
        :class="{ 'is-highlighted': is_playing }"
      >
        <span
          v-for="word in verse.words"
          :key="word"
          class="has-text-weight-bold is-size-5"
        >
          <span
            :style="{ 'animation-duration': `${word.delay}s` }"
            v-text="word.text"
          />
        </span>
      </div>
      <div v-else>
        {{ verse.text }}
      </div>
    </template>
  </div>
</template>

<script>
export default {
  name: 'LyricsPane',
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
    is_playing() {
      return this.player.state === 'play'
    },
    lyrics() {
      const raw = this.$store.state.lyrics.content
      const parsed = []
      if (raw) {
        // Parse the lyrics
        const regex = /(\[(\d+):(\d+)(?:\.\d+)?\] ?)?(.*)/u
        raw.split('\n').forEach((item, index) => {
          const matches = regex.exec(item)
          if (matches && matches[4]) {
            const verse = {
              text: matches[4],
              time: matches[2] * 60 + Number(matches[3])
            }
            parsed.push(verse)
          }
        })
        // Split the verses into words
        parsed.forEach((verse, index, lyrics) => {
          const duration =
            index < lyrics.length - 1 ? lyrics[index + 1].time - verse.time : 3
          const unitDuration = duration / verse.text.length
          let delay = 0
          verse.words = verse.text.match(/\S+\s*/gu).map((text) => {
            const duration = text.length * unitDuration
            delay += duration
            return {
              duration,
              delay,
              text
            }
          })
        })
      }
      return parsed
    },
    player() {
      return this.$store.state.player
    },
    verse_index() {
      if (this.lyrics.length && this.lyrics[0].time) {
        const currentTime = this.player.item_progress_ms / 1000,
          la = this.lyrics,
          trackChanged = this.player.item_id !== this.lastItemId,
          trackSeeked =
            this.lastIndex >= 0 &&
            this.lastIndex < la.length &&
            la[this.lastIndex].time > currentTime
        // Reset the cache when the track has changed or has been seeked
        if (trackChanged || trackSeeked) {
          this.reset_scrolling()
        }
        // Check the next two items and the last one before searching
        if (
          (this.lastIndex < la.length - 1 &&
            la[this.lastIndex + 1].time > currentTime) ||
          this.lastIndex === la.length - 1
        ) {
          return this.lastIndex
        }
        if (
          this.lastIndex < la.length - 2 &&
          la[this.lastIndex + 2].time > currentTime
        ) {
          return this.lastIndex + 1
        }
        // Not found, then start a binary search
        let end = la.length - 1,
          index = -1,
          start = 0
        while (start <= end) {
          index = (start + end) >> 1
          const currentVerseTime = la[index].time
          const nextVerseTime = la[index + 1]?.time
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
      this.reset_scrolling()
      return -1
    }
  },
  watch: {
    verse_index() {
      if (this.autoScrolling) {
        this.scroll_to_verse()
      }
      this.lastIndex = this.verse_index
    }
  },
  methods: {
    reset_scrolling() {
      // Scroll to the start of the lyrics in all cases
      if (this.player.item_id !== this.lastItemId && this.$refs.lyrics) {
        this.$refs.lyrics.scrollTo(0, 0)
      }
      this.lastItemId = this.player.item_id
      this.lastIndex = -1
    },
    scroll_to_verse() {
      const pane = this.$refs.lyrics
      if (this.verse_index === -1) {
        pane.scrollTo(0, 0)
        return
      }
      const currentVerse = pane.children[this.verse_index]
      pane.scrollBy({
        behavior: 'smooth',
        left: 0,
        top:
          currentVerse.offsetTop -
          (pane.offsetHeight >> 1) +
          (currentVerse.offsetHeight >> 1) -
          pane.scrollTop
      })
    },
    start_scrolling(event) {
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
  top: 0;
  left: calc(50% - 50vw);
  width: 100vw;
  height: calc(100vh - 26rem);
  max-height: min(100% - 8rem, 100vh - 26rem + 3.5rem);
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
.lyrics div {
  line-height: 3rem;
}
.lyrics div:first-child {
  padding-top: calc(25vh - 2rem);
}

.lyrics div:last-child {
  padding-bottom: calc(25vh - 3rem);
}
</style>
