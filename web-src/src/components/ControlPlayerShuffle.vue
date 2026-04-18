<template>
  <button :class="{ 'is-dark': playerStore.shuffle }" @click="toggle">
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
  if (playerStore.shuffle) {
    return 'shuffle'
  }
  return 'shuffle-disabled'
})

const toggle = () => {
  player.shuffle(!playerStore.shuffle)
}
</script>
