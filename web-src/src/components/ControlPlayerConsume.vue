<template>
  <a :class="{ 'is-dark': playerStore.consume }" @click="toggle">
    <mdicon
      class="icon"
      name="fire"
      size="16"
      :title="$t('player.button.consume')"
    />
  </a>
</template>

<script>
import { usePlayerStore } from '@/stores/player'
import webapi from '@/webapi'

export default {
  name: 'ControlPlayerConsume',
  setup() {
    return {
      playerStore: usePlayerStore()
    }
  },
  methods: {
    toggle() {
      webapi.player_consume(!this.playerStore.consume)
    }
  }
}
</script>
