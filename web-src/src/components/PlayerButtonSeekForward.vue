<template>
  <a v-if="visible" :disabled="disabled" @click="seek">
    <mdicon
      name="fast-forward-30"
      :size="icon_size"
      :title="$t('player.button.seek-forward')"
    />
  </a>
</template>

<script>
import { usePlayerStore } from '@/stores/player'
import { useQueueStore } from '@/stores/queue'
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonSeekForward',
  props: {
    icon_size: { default: 16, type: Number },
    seek_ms: { required: true, type: Number }
  },

  setup() {
    return {
      playerStore: usePlayerStore(),
      queueStore: useQueueStore()
    }
  },

  computed: {
    disabled() {
      return (
        this.queueStore?.count <= 0 ||
        this.is_stopped ||
        this.current.data_kind === 'pipe'
      )
    },
    is_stopped() {
      return this.player.state === 'stop'
    },
    current() {
      return this.queueStore.current
    },
    player() {
      return this.playerStore
    },
    visible() {
      return ['podcast', 'audiobook'].includes(this.current.media_kind)
    }
  },

  methods: {
    seek() {
      if (!this.disabled) {
        webapi.player_seek(this.seek_ms)
      }
    }
  }
}
</script>
