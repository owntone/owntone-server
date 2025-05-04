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

<script>
import player from '@/api/player'
import { usePlayerStore } from '@/stores/player'

export default {
  name: 'ControlPlayerConsume',
  setup() {
    return {
      playerStore: usePlayerStore()
    }
  },
  methods: {
    toggle() {
      player.consume(!this.playerStore.consume)
    }
  }
}
</script>
