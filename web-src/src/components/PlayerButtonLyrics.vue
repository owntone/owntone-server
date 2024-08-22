<template>
  <a :class="{ 'is-info': is_active }" @click="toggle_lyrics">
    <mdicon
      class="icon"
      :name="icon_name"
      :size="icon_size"
      :title="$t('player.button.toggle-lyrics')"
    />
  </a>
</template>

<script>
import { useLyricsStore } from '@/stores/lyrics'

export default {
  name: 'PlayerButtonLyrics',
  props: {
    icon_size: { default: 16, type: Number }
  },

  setup() {
    return {
      lyricsStore: useLyricsStore()
    }
  },

  computed: {
    icon_name() {
      return this.is_active ? 'script-text-play' : 'script-text-outline'
    },
    is_active() {
      return this.lyricsStore.pane
    }
  },

  methods: {
    toggle_lyrics() {
      this.lyricsStore.pane = !this.lyricsStore.pane
    }
  }
}
</script>

<style></style>
