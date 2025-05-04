<template>
  <button :class="{ 'is-dark': playerStore.lyrics }" @click="toggle">
    <mdicon
      class="icon"
      :name="icon"
      :size="16"
      :title="$t('player.button.toggle-lyrics')"
    />
  </button>
</template>

<script>
import { usePlayerStore } from '@/stores/player'

export default {
  name: 'ControlPlayerLyrics',
  setup() {
    return {
      playerStore: usePlayerStore()
    }
  },
  computed: {
    icon() {
      return this.playerStore.lyrics
        ? 'script-text-play'
        : 'script-text-outline'
    }
  },
  methods: {
    toggle() {
      this.playerStore.lyrics = !this.playerStore.lyrics
    }
  }
}
</script>
