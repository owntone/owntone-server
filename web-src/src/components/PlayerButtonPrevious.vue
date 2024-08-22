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
import { useQueueStore } from '@/stores/queue'
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonPrevious',
  props: {
    icon_size: { default: 16, type: Number }
  },

  setup() {
    return {
      queueStore: useQueueStore()
    }
  },

  computed: {
    disabled() {
      return this.queueStore.count <= 0
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
