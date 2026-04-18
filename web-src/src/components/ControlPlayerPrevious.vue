<template>
  <button :disabled="queueStore.isEmpty" @click="previous">
    <mdicon
      class="icon"
      name="skip-backward"
      :title="$t('player.button.skip-backward')"
    />
  </button>
</template>

<script setup>
import player from '@/api/player'
import { useQueueStore } from '@/stores/queue'

const queueStore = useQueueStore()

const previous = () => {
  player.previous()
}
</script>
