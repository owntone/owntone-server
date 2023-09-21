<template>
  <div
    class="lyrics-wrapper"
    ref="lyricsWrapper"
    @touchstart="autoScroll = false"
    @touchend="autoScroll = true"
    v-on:scroll.passive="startedScroll"
    v-on:wheel.passive="startedScroll"
  >
    <div class="lyrics">
      <p
        v-for="(item, key) in lyricsArr"
        :class="key == lyricIndex && is_sync && 'gradient'"
      >
        {{ item[0] }}
      </p>
    </div>
  </div>
</template>

<script>

export default {
  name: "lyrics",
  data() {
    // Non reactive
    // Used as a cache to speed up finding the lyric index in the array for the current time
    this.lastIndex = 0;
    // Fired upon scrolling, that's disabling the auto scrolling for 5s
    this.scrollTimer = null;
    this.lastItemId = -1;
    // Reactive
    return {
      scroll: {},
      // lineHeight: 42,
      autoScroll: true, // stop scroll to element when touch
    };
  },
  computed: {
    player() {
      return this.$store.state.player
    },

    is_sync() {
      return this.lyricsArr.length && this.lyricsArr[0].length > 1;
    },
  
    lyricIndex() {
      // We have to perform a dichotomic search in the time array to find the index that's matching
      const curTime = this.player.item_progress_ms / 1000;
      const la = this.lyricsArr;
      if (la.length && la[0].length === 1) return 0; // Bail out for non synchronized lyrics
      if (this.player.item_id != this.lastItemId 
       || this.lastIndex < la.length && la[this.lastIndex][1] > curTime) {
        // Song changed or time scrolled back, let's reset the cache
        this.lastItemId = this.player.item_id;
        this.lastIndex = 0;
      }
      // Check the cached value to avoid searching the times
      if (this.lastIndex < la.length - 1 && la[this.lastIndex + 1][1] > curTime)
        return this.lastIndex;
      if (this.lastIndex < la.length - 2 && la[this.lastIndex + 2][1] > curTime)
        return this.lastIndex + 1;

      // Not found in the next 2 items, so start dichotomic search for the best time
      let i;
      let start = 0,
        end = la.length - 1;
      while (start <= end) {
        i = ((end + start) / 2) | 0;
        if (la[i][1] <= curTime && ((la.length > i+1) && la[i + 1][1] > curTime)) break;
        if (la[i][1] < curTime) start = i + 1;
        else end = i - 1;
      }
      return i;
    },
    lyricDuration() {
      // Ignore unsynchronized lyrics.
      if (!this.lyricsArr.length || this.lyricsArr[0].length < 2) return 3600;
      // The index is 0 before the first lyric until the end of the first lyric
      if (!this.lyricIndex && this.player.item_progress_ms / 1000 < this.lyricsArr[0][1])
        return this.lyricsArr[0][1];
      return this.lyricIndex < this.lyricsArr.length - 1
        ? this.lyricsArr[this.lyricIndex + 1][1] -
            this.lyricsArr[this.lyricIndex][1]
        : 3600;
    },
    lyricsArr() {
      return this.$store.getters.lyrics;
    },
  },
  watch: {
    lyricIndex() {
      // Scroll current lyric in the center of the view unless user manipulated
      this.autoScroll && this._scrollToElement();
      this.lastIndex = this.lyricIndex;
    },
  },
  methods: {
    startedScroll(e) {
      // Ugly trick to check if a scroll event comes from the user or from JS
      if (!e.screenX || e.screenX == 0 || !e.screenY || e.screenY == 0) return; // Programmatically triggered event are ignored here
      this.autoScroll = false;
      if (this.scrollTimer) clearTimeout(this.scrollTimer);
      let t = this;
      // Re-enable automatic scrolling after 5s
      this.scrollTimer = setTimeout(function () {
        t.autoScroll = true;
      }, 5000);
    },

    _scrollToElement() {
      let scrollTouch = this.$refs.lyricsWrapper,
        currentLyric = scrollTouch.children[0].children[this.lyricIndex],
        offsetToCenter = scrollTouch.offsetHeight >> 1;
      if (!this.lyricsArr || !currentLyric) return;

      let currOff = scrollTouch.scrollTop,
        destOff = currentLyric.offsetTop - offsetToCenter;
      // Using scrollBy ensure that scrolling will happen
      // even if the element is visible before scrolling
      scrollTouch.scrollBy({
        top: destOff - currOff,
        left: 0,
        behavior: "smooth",
      });

      // Then prepare the animated gradient too
      currentLyric.style.animationDuration = this.lyricDuration + "s";
    },
  },
};
</script>

<style scoped>
.lyrics-wrapper {
  position: absolute;
  top: -1rem;
  left: calc(50% - 40vw);
  right: calc(50% - 40vw);
  bottom: 0;
  max-height: calc(100% - 9rem);
  overflow: auto;

  /* Glass effect */
  background: rgba(255, 255, 255, 0.8);
  box-shadow: 0 4px 30px rgba(0, 0, 0, 0.1);
  backdrop-filter: blur(3px);
  -webkit-backdrop-filter: blur(3px);
  border: 1px solid rgba(0, 0, 0, 0.3);
}
.lyrics-wrapper .lyrics {
  display: flex;
  align-items: center;
  flex-direction: column;
}
.lyrics-wrapper .lyrics .gradient {
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
  font-weight: bold;
  font-size: 120%;
  animation: slide-right 1 linear;
  background-size: 200% 100%;

  background-image: -webkit-linear-gradient(
    left,
    #080 50%,
    #000 50%
  );
}

@keyframes slide-right {
  0% {
    background-position: 100% 0%;
  }
  100% {
    background-position: 0% 0%;
  }
}

.lyrics-wrapper .lyrics p {
  line-height: 3rem;
  text-align: center;
  font-size: 1rem;
  color: #000;
}
</style>
