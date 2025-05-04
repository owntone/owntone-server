<template>
  <button
    :class="{ 'is-dark': lyricsStore.active }"
    @click="lyricsStore.toggle"
  >
    <mdicon
      class="icon"
      :name="icon"
      :size="16"
      :title="$t('player.button.toggle-lyrics')"
    />
  </button>
</template>

<script>
import { useLyricsStore } from '@/stores/lyrics'

export default {
  name: 'ControlPlayerLyrics',
  setup() {
    return {
      lyricsStore: useLyricsStore()
    }
  },
  computed: {
    icon() {
      return this.lyricsStore.active
        ? 'script-text-play'
        : 'script-text-outline'
    }
  }
}
</script>
