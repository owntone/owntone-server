<template>
  <button :disabled="queueStore.isEmpty" @click="next">
    <mdicon
      class="icon"
      name="skip-forward"
      :title="$t('player.button.skip-forward')"
    />
  </button>
</template>

<script setup>
import player from '@/api/player'
import { useQueueStore } from '@/stores/queue'

const queueStore = useQueueStore()

const next = () => {
  player.next()
}
</script>
