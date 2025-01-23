<template>
  <a :disabled="disabled" @click="play_next">
    <mdicon
      name="skip-forward"
      :size="icon_size"
      :title="$t('player.button.skip-forward')"
    />
  </a>
</template>

<script>
import { useQueueStore } from '@/stores/queue'
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonNext',
  props: {
    icon_size: { default: 16, type: Number }
  },

  computed: {
    disabled() {
      return useQueueStore()?.count <= 0
    }
  },

  methods: {
    play_next() {
      if (this.disabled) {
        return
      }

      webapi.player_next()
    }
  }
}
</script>

<style></style>
