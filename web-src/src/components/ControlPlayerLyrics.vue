<template>
  <button :class="{ 'is-dark': playerStore.showLyrics }" @click="toggle">
    <mdicon
      class="icon"
      :name="icon"
      :size="16"
      :title="$t('player.button.toggle-lyrics')"
    />
  </button>
</template>

<script setup>
import { computed } from 'vue'
import { usePlayerStore } from '@/stores/player'

const playerStore = usePlayerStore()

const icon = computed(
  () => (playerStore.showLyrics && 'script-text-play') || 'script-text-outline'
)

const toggle = () => {
  playerStore.showLyrics = !playerStore.showLyrics
}
</script>
