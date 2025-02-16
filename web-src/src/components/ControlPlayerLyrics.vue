<template>
  <a :class="{ 'is-dark': is_active }" @click="toggle">
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
      return this.is_active ? 'script-text-play' : 'script-text-outline'
    },
    is_active() {
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
