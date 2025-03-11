<template>
  <a :disabled="disabled" @click="toggle">
    <mdicon class="icon" :name="icon" :title="$t(`player.button.${icon}`)" />
  </a>
</template>

<script>
import { useNotificationsStore } from '@/stores/notifications'
import { usePlayerStore } from '@/stores/player'
import { useQueueStore } from '@/stores/queue'
import webapi from '@/webapi'

export default {
  name: 'ControlPlayerPlay',
  props: {
    show_disabled_message: Boolean
  },
  setup() {
    return {
      notificationsStore: useNotificationsStore(),
      playerStore: usePlayerStore(),
      queueStore: useQueueStore()
    }
  },
  computed: {
    disabled() {
      return this.queueStore?.count <= 0
    },
    icon() {
      if (!this.playerStore.isPlaying) {
        return 'play'
      } else if (this.queueStore.isPauseAllowed) {
        return 'pause'
      }
      return 'stop'
    }
  },
  methods: {
    toggle() {
      if (this.disabled) {
        if (this.show_disabled_message) {
          this.notificationsStore.add({
            text: this.$t('server.empty-queue'),
            timeout: 2000,
            topic: 'connection',
            type: 'info'
          })
        }
        return
      }
      if (this.playerStore.isPlaying && this.queueStore.isPauseAllowed) {
        webapi.player_pause()
      } else if (
        this.playerStore.isPlaying &&
        !this.queueStore.isPauseAllowed
      ) {
        webapi.player_stop()
      } else {
        webapi.player_play()
      }
    }
  }
}
</script>
