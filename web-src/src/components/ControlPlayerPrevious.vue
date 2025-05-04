<template>
  <a :disabled="disabled" @click="previous">
    <mdicon
      class="icon"
      name="skip-backward"
      :title="$t('player.button.skip-backward')"
    />
  </a>
</template>

<script>
import player from '@/api/player'
import { useQueueStore } from '@/stores/queue'

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
    previous() {
      player.previous()
    }
  }
}
</script>
