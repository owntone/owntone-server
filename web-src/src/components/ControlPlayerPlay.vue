<template>
  <button :disabled="queueStore.isEmpty" @click="toggle">
    <mdicon class="icon" :name="icon" :title="$t(`player.button.${icon}`)" />
  </button>
</template>

<script setup>
import { computed } from 'vue'
import player from '@/api/player'
import { usePlayerStore } from '@/stores/player'
import { useQueueStore } from '@/stores/queue'

const playerStore = usePlayerStore()
const queueStore = useQueueStore()

const icon = computed(() => {
  if (!playerStore.isPlaying) {
    return 'play'
  } else if (queueStore.isPauseAllowed) {
    return 'pause'
  }
  return 'stop'
})

const toggle = () => {
  if (playerStore.isPlaying && queueStore.isPauseAllowed) {
    player.pause()
  } else if (playerStore.isPlaying && !queueStore.isPauseAllowed) {
    player.stop()
  } else {
    player.play()
  }
}
</script>
