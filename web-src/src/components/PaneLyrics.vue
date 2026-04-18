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

<script setup>
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { usePlayerStore } from '@/stores/player'

const VISIBLE_VERSES = 7
const MIDDLE_POSITION = Math.floor(VISIBLE_VERSES / 2)

const playerStore = usePlayerStore()

const lastUpdateTime = ref(0)
const lyrics = ref({ synchronised: false, verses: [] })
const time = ref(0)
const timerId = ref(null)

const verseIndex = (verses) => {
  const currentTime = time.value
  let low = 0
  let high = verses.length - 1
  while (low <= high) {
    const mid = Math.floor((low + high) / 2),
      midTime = verses[mid].time,
      nextTime = verses[mid + 1]?.time
    if (midTime <= currentTime && (!nextTime || nextTime > currentTime)) {
      return mid
    } else if (midTime < currentTime) {
      low = mid + 1
    } else {
      high = mid - 1
    }
  }
  return -1
}

const tick = () => {
  time.value = playerStore.item_progress_ms + Date.now() - lastUpdateTime.value
}

const startTimer = () => {
  if (timerId.value) {
    return
  }
  timerId.value = setInterval(tick, 100)
}

const stopTimer = () => {
  if (timerId.value) {
    clearInterval(timerId.value)
    timerId.value = null
  }
}

const updateTime = () => {
  lastUpdateTime.value = Date.now()
  if (playerStore.isPlaying) {
    startTimer()
  } else {
    time.value = playerStore.item_progress_ms
  }
}

const visibleVerses = computed(() => {
  const { verses, synchronised } = lyrics.value
  let start = 0
  let { length } = verses
  if (synchronised) {
    start = verseIndex(verses) - MIDDLE_POSITION
    length = VISIBLE_VERSES
  }
  return Array.from(
    { length },
    (_, i) => verses[start + i] ?? { text: '\u00A0' }
  )
})

const parseLyrics = () => {
  const parsed = { synchronised: false, verses: [] }
  const regex =
    /(?:\[(?<minutes>\d+):(?<seconds>\d+)(?:\.(?<hundredths>\d+))?\])?\s*(?<text>\S.*\S)?\s*/u
  playerStore.lyricsContent.split('\n').forEach((line) => {
    const match = regex.exec(line)
    if (match) {
      const { text, minutes, seconds, hundredths } = match.groups
      const verse = text
      if (verse) {
        const position =
          (Number(minutes) * 60 + Number(`${seconds}.${hundredths ?? 0}`)) *
          1000
        parsed.synchronised = !isNaN(position)
        parsed.verses.push({ text: verse, time: position })
      }
    }
  })
  parsed.verses.forEach((verse, index, verses) => {
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
  return parsed
}

watch(
  () => playerStore.isPlaying,
  (isPlaying) => {
    if (isPlaying) {
      lastUpdateTime.value = Date.now()
      startTimer()
    } else {
      stopTimer()
    }
  }
)

watch(
  () => playerStore.item_progress_ms,
  (progress) => {
    lastUpdateTime.value = Date.now()
    if (!playerStore.isPlaying) {
      time.value = progress
    }
  }
)

watch(
  () => playerStore.lyricsContent,
  () => {
    lyrics.value = parseLyrics()
  }
)

onMounted(() => {
  playerStore.initialise()
  lastUpdateTime.value = Date.now()
  lyrics.value = parseLyrics()
  updateTime()
})

onBeforeUnmount(() => {
  stopTimer()
})

const isVerseHighlighted = (index) =>
  index === MIDDLE_POSITION && lyrics.value.synchronised

const isWordHighlighted = (word) =>
  time.value >= word.start && time.value < word.end
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
