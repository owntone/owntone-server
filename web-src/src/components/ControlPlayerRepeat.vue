<template>
  <a :class="{ 'is-dark': !playerStore.isRepeatOff }" @click="toggle">
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
        webapi.player_repeat('single')
      } else if (this.playerStore.isRepeatSingle) {
        webapi.player_repeat('off')
      } else {
        webapi.player_repeat('all')
      }
    }
  }
}
</script>
