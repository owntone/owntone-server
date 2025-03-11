<template>
  <a :disabled="disabled" @click="next">
    <mdicon
      class="icon"
      name="skip-forward"
      :title="$t('player.button.skip-forward')"
    />
  </a>
</template>

<script>
import { useQueueStore } from '@/stores/queue'
import webapi from '@/webapi'

export default {
  name: 'ControlPlayerNext',
  setup() {
    return { queueStore: useQueueStore() }
  },
  computed: {
    disabled() {
      return this.queueStore.count <= 0
    }
  },
  methods: {
    next() {
      if (this.disabled) {
        return
      }
      webapi.player_next()
    }
  }
}
</script>
