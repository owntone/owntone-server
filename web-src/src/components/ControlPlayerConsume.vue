<template>
  <button :class="{ 'is-dark': playerStore.consume }" @click="toggle">
    <mdicon
      class="icon"
      name="fire"
      size="16"
      :title="$t('player.button.consume')"
    />
  </button>
</template>

<script setup>
import player from '@/api/player'
import { usePlayerStore } from '@/stores/player'

const playerStore = usePlayerStore()

const toggle = () => {
  player.consume(!playerStore.consume)
}
</script>
