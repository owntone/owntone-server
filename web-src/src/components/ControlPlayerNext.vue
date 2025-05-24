<template>
  <button :disabled="disabled" @click="next">
    <mdicon
      class="icon"
      name="skip-forward"
      :title="$t('player.button.skip-forward')"
    />
  </button>
</template>

<script>
import player from '@/api/player'
import { useQueueStore } from '@/stores/queue'

export default {
  name: 'ControlPlayerNext',
  setup() {
    return { queueStore: useQueueStore() }
  },
  computed: {
    disabled() {
      return this.queueStore.isEmpty
    }
  },
  methods: {
    next() {
      player.next()
    }
  }
}
</script>
