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
      return this.$store.state.lyrics.pane
    },
    icon_name() {
      return this.is_active ? 'script-text-play' : 'script-text-outline'
    }
  },

  methods: {
    toggle_lyrics() {
      this.$store.state.lyrics.pane = !this.$store.state.lyrics.pane
    }
  }
}
</script>

<style></style>
