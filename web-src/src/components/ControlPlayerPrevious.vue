<template>
  <a :disabled="disabled" @click="playPrevious">
    <mdicon
      class="icon"
      name="skip-backward"
      :title="$t('player.button.skip-backward')"
    />
  </a>
</template>

<script>
import { useQueueStore } from '@/stores/queue'
import webapi from '@/webapi'

export default {
  name: 'ControlPlayerPrevious',
  setup() {
    return { queueStore: useQueueStore() }
  },
  computed: {
    disabled() {
      return this.queueStore.count <= 0
    }
  },
  methods: {
    playPrevious() {
      if (this.disabled) {
        return
      }
      webapi.player_previous()
    }
  }
}
</script>
