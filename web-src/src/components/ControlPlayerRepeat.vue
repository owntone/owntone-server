<template>
  <button :class="{ 'is-dark': !playerStore.isRepeatOff }" @click="toggle">
    <mdicon
      class="icon"
      :name="icon"
      :size="16"
      :title="$t(`player.button.${icon}`)"
    />
  </button>
</template>

<script setup>
import { computed } from 'vue'
import player from '@/api/player'
import { usePlayerStore } from '@/stores/player'

const playerStore = usePlayerStore()

const icon = computed(() => {
  if (playerStore.isRepeatAll) {
    return 'repeat'
  } else if (playerStore.isRepeatSingle) {
    return 'repeat-once'
  }
  return 'repeat-off'
})

const toggle = () => {
  if (playerStore.isRepeatAll) {
    player.repeat('single')
  } else if (playerStore.isRepeatSingle) {
    player.repeat('off')
  } else {
    player.repeat('all')
  }
}
</script>
