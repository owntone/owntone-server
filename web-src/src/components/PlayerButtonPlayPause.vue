<template>
  <a :disabled="disabled" @click="toggle_play_pause">
    <mdicon
      :name="icon_name"
      :size="icon_size"
      :title="$t(`player.button.${icon_name}`)"
    />
  </a>
</template>

<script>
import { useNotificationsStore } from '@/stores/notifications'
import { usePlayerStore } from '@/stores/player'
import { useQueueStore } from '@/stores/queue'
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonPlayPause',
  props: {
    icon_size: { default: 16, type: Number },
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
    icon_name() {
      if (!this.is_playing) {
        return 'play'
      } else if (this.is_pause_allowed) {
        return 'pause'
      }
      return 'stop'
    },
    is_pause_allowed() {
      const { current } = this.queueStore
      return current && current.data_kind !== 'pipe'
    },
    is_playing() {
      return this.playerStore.state === 'play'
    }
  },

  methods: {
    toggle_play_pause() {
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
      if (this.is_playing && this.is_pause_allowed) {
        webapi.player_pause()
      } else if (this.is_playing && !this.is_pause_allowed) {
        webapi.player_stop()
      } else {
        webapi.player_play()
      }
    }
  }
}
</script>

<style></style>
