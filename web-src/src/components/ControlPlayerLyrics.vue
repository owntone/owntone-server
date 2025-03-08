<template>
  <a :class="{ 'is-dark': isActive }" @click="toggle">
    <mdicon
      class="icon"
      :name="icon"
      :size="16"
      :title="$t('player.button.toggle-lyrics')"
    />
  </a>
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
      return this.isActive ? 'script-text-play' : 'script-text-outline'
    },
    isActive() {
      return this.lyricsStore.pane
    }
  },
  methods: {
    toggle() {
      this.lyricsStore.pane = !this.lyricsStore.pane
    }
  }
}
</script>
