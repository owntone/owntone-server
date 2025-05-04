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
import player from '@/api/player'
import { useQueueStore } from '@/stores/queue'

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
      player.next()
    }
  }
}
</script>
