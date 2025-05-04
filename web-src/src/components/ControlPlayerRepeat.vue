<template>
  <button :class="{ 'is-dark': !playerStore.isRepeatOff }" @click="toggle">
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
  name: 'ControlPlayerRepeat',
  setup() {
    return { playerStore: usePlayerStore() }
  },
  computed: {
    icon() {
      if (this.playerStore.isRepeatAll) {
        return 'repeat'
      } else if (this.playerStore.isRepeatSingle) {
        return 'repeat-once'
      }
      return 'repeat-off'
    }
  },
  methods: {
    toggle() {
      if (this.playerStore.isRepeatAll) {
        player.repeat('single')
      } else if (this.playerStore.isRepeatSingle) {
        player.repeat('off')
      } else {
        player.repeat('all')
      }
    }
  }
}
</script>
