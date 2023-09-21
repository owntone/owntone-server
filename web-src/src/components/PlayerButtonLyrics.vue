<template>
  <a :class="{ 'is-active': is_active }" @click="toggle_lyrics">
    <mdicon
      v-if="!is_active"
      name="script-text-outline"
      :size="icon_size"
      :title="$t('player.button.toggle-lyrics')"
    />
    <mdicon
      v-if="is_active"
      name="script-text-play"
      :size="icon_size"
      :title="$t('player.button.toggle-lyrics')"
    />
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonLyrics',

  props: {
    icon_size: {
      type: Number,
      default: 16
    }
  },

  computed: {
    is_active() {
      return this.$store.getters.lyrics_pane;
    }
  },

  methods: {
    toggle_lyrics() {
      this.$store.state.lyrics.lyrics_pane = !this.$store.state.lyrics.lyrics_pane;
    }
  }
}
</script>

<style></style>
