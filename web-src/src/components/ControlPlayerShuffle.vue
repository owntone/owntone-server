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

<script>
import player from '@/api/player'
import { usePlayerStore } from '@/stores/player'

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
      player.shuffle(!this.playerStore.shuffle)
    }
  }
}
</script>
