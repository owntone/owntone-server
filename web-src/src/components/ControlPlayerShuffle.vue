<template>
  <a :class="{ 'is-dark': playerStore.shuffle }" @click="toggle">
    <mdicon
      class="icon"
      :name="icon"
      :size="16"
      :title="$t(`player.button.${icon}`)"
    />
  </a>
</template>

<script>
import { usePlayerStore } from '@/stores/player'
import webapi from '@/webapi'

export default {
  name: 'ControlPlayerShuffle',
  setup() {
    return { playerStore: usePlayerStore() }
  },
  computed: {
    icon() {
      if (this.playerStore.shuffle) {
        return 'shuffle'
      }
      return 'shuffle-disabled'
    }
  },
  methods: {
    toggle() {
      webapi.player_shuffle(!this.playerStore.shuffle)
    }
  }
}
</script>
