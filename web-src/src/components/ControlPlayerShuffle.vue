<template>
  <a :class="{ 'is-info': is_shuffle }" @click="toggle">
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
      if (this.is_shuffle) {
        return 'shuffle'
      }
      return 'shuffle-disabled'
    },
    is_shuffle() {
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
