<template>
  <a :class="{ 'is-dark': !is_repeat_off }" @click="toggle">
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
    return {
      playerStore: usePlayerStore()
    }
  },
  computed: {
    icon() {
      if (this.is_repeat_all) {
        return 'repeat'
      } else if (this.is_repeat_single) {
        return 'repeat-once'
      }
      return 'repeat-off'
    },
    is_repeat_all() {
      return this.playerStore.repeat === 'all'
    },
    is_repeat_off() {
      return !this.is_repeat_all && !this.is_repeat_single
    },
    is_repeat_single() {
      return this.playerStore.repeat === 'single'
    }
  },

  methods: {
    toggle() {
      if (this.is_repeat_all) {
        webapi.player_repeat('single')
      } else if (this.is_repeat_single) {
        webapi.player_repeat('off')
      } else {
        webapi.player_repeat('all')
      }
    }
  }
}
</script>
