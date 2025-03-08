<template>
  <a :class="{ 'is-dark': isShuffle }" @click="toggle">
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
    return {
      playerStore: usePlayerStore()
    }
  },
  computed: {
    icon() {
      if (this.isShuffle) {
        return 'shuffle'
      }
      return 'shuffle-disabled'
    },
    isShuffle() {
      return this.playerStore.shuffle
    }
  },
  methods: {
    toggle() {
      webapi.player_shuffle(!this.is_shuffle)
    }
  }
}
</script>
