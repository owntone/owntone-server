<template>
  <div
    ref="lyrics"
    class="lyrics"
    @touchstart="autoScrolling = false"
    @touchend="autoScrolling = true"
    @scroll.passive="start_scrolling"
    @wheel.passive="start_scrolling"
  >
    <template v-for="(item, index) in lyrics" :key="item">
      <div
        v-if="is_sync && index == verse_index"
        :class="{ 'is-highlighted': is_playing }"
      >
        <span
          v-for="word in split_verse(index)"
          :key="word.duration"
          class="has-text-weight-bold is-size-5"
        >
          <span
            :style="{ animationDuration: word.duration + 's' }"
            v-text="word.content"
          />
        </span>
      </div>
      <div v-else>
        {{ item[0] }}
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
    this.scrollTimer = null
    this.lastItemId = -1
    return {
      autoScrolling: true
    }
  },
  computed: {
    is_playing() {
      return this.player.state === 'play'
    },
    is_sync() {
      return this.lyrics.length && this.lyrics[0].length > 1
    },
    verse_index() {
      if (!this.is_sync) {
        this.reset_scrolling()
        return -1
      }
      const curTime = this.player.item_progress_ms / 1000,
        la = this.lyrics,
        trackChanged = this.player.item_id !== this.lastItemId,
        trackSeeked =
          this.lastIndex >= 0 &&
          this.lastIndex < la.length &&
          la[this.lastIndex][1] > curTime
      if (trackChanged || trackSeeked) {
        // Reset the cache when the track has changed or has been rewind
        this.reset_scrolling()
      }
      // Check the cached value to avoid searching the times
      if (this.lastIndex < la.length - 1 && la[this.lastIndex + 1][1] > curTime)
        return this.lastIndex
      if (this.lastIndex < la.length - 2 && la[this.lastIndex + 2][1] > curTime)
        return this.lastIndex + 1
      // Not found in the next 2 items, so start searching for the best time
      return la.findLastIndex((verse) => verse[1] <= curTime)
    },
    lyrics() {
      const lyrics = this.$store.getters.lyrics
      const lyricsObj = []
      if (lyrics) {
        const regex = /(\[(\d+):(\d+)(?:\.\d+)?\] ?)?(.*)/
        lyrics.split('\n').forEach((item) => {
          const matches = regex.exec(item)
          if (matches && matches[4]) {
            const obj = [matches[4]]
            if (matches[2] && matches[3])
              obj.push(matches[2] * 60 + matches[3] * 1)
            lyricsObj.push(obj)
          }
        })
      }
      return lyricsObj
    },
    player() {
      return this.$store.state.player
    }
  },
  watch: {
    verse_index() {
      this.autoScrolling && this.scroll_to_verse()
      this.lastIndex = this.verse_index
    }
  },
  methods: {
    reset_scrolling() {
      // Scroll to the start of the track lyrics in all cases
      if (this.player.item_id != this.lastItemId && this.$refs.lyrics) {
        this.$refs.lyrics.scrollTo(0, 0)
      }
      this.lastItemId = this.player.item_id
      this.lastIndex = -1
    },
    start_scrolling(e) {
      /*
       * Distinguish scroll event triggered by a user or programmatically
       * Programmatically triggered event are ignored
       */
      if (!e.screenX || e.screenX == 0 || !e.screenY || e.screenY == 0) return
      this.autoScrolling = false
      if (this.scrollTimer) {
        clearTimeout(this.scrollTimer)
      }
      const t = this
      // Reenable automatic scrolling after 5 seconds
      this.scrollTimer = setTimeout(() => {
        t.autoScrolling = true
      }, 2000)
    },
    scroll_to_verse() {
      const pane = this.$refs.lyrics
      if (this.verse_index === -1) {
        pane.scrollTo(0, 0)
        return
      }
      const currentVerse = pane.children[this.verse_index]
      const offsetToCenter = pane.offsetHeight >> 1
      if (!this.lyrics || !currentVerse) return
      const top =
        currentVerse.offsetTop -
        offsetToCenter +
        (currentVerse.offsetHeight >> 1) -
        pane.scrollTop
      /*
       * Using scrollBy ensures the scrolling will happen
       * even if the element is visible before scrolling
       */
      pane.scrollBy({
        top,
        left: 0,
        behavior: 'smooth'
      })
    },
    split_verse(index) {
      const verse = this.lyrics[index]
      let verseDuration = 3 // Default duration for a verse
      if (index < this.lyrics.length - 1) {
        verseDuration = this.lyrics[index + 1][1] - verse[1]
      }
      const unitDuration = verseDuration / verse[0].length
      // Split verse into words
      let duration = 0
      return verse[0].match(/\S+\s*/g).map((word) => {
        const d = duration
        duration += word.length * unitDuration
        return { duration: d, content: word }
      })
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
