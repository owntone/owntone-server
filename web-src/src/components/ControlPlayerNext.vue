<template>
  <a :disabled="disabled" @click="play_next">
    <mdicon
      class="icon"
      name="skip-forward"
      :title="$t('player.button.skip-forward')"
    />
  </a>
</template>

<script>
import { useQueueStore } from '@/stores/queue'
import webapi from '@/webapi'

export default {
  name: 'ControlPlayerNext',

  computed: {
    disabled() {
      return useQueueStore()?.count <= 0
    }
  },

  methods: {
    play_next() {
      if (this.disabled) {
        return
      }
      webapi.player_next()
    }
  }
}
</script>
