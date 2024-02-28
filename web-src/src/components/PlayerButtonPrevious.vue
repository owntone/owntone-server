<template>
  <a :disabled="disabled" @click="play_previous">
    <mdicon
      name="skip-backward"
      :size="icon_size"
      :title="$t('player.button.skip-backward')"
    />
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonPrevious',
  props: {
    icon_size: { default: 16, type: Number }
  },

  computed: {
    disabled() {
      return !this.$store.state.queue || this.$store.state.queue.count <= 0
    }
  },

  methods: {
    play_previous() {
      if (this.disabled) {
        return
      }

      webapi.player_previous()
    }
  }
}
</script>

<style></style>
