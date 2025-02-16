<template>
  <a :class="{ 'is-dark': is_consume }" @click="toggle">
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

  computed: {
    is_consume() {
      return this.playerStore.consume
    }
  },

  methods: {
    toggle() {
      webapi.player_consume(!this.is_consume)
    }
  }
}
</script>
