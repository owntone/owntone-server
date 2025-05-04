<template>
  <a v-if="visible" :disabled="disabled" @click="seek">
    <mdicon
      class="icon"
      name="rewind-10"
      :title="$t('player.button.seek-backward')"
    />
  </a>
</template>

<script>
import player from '@/api/player'
import { usePlayerStore } from '@/stores/player'
import { useQueueStore } from '@/stores/queue'

export default {
  name: 'ControlPlayerBack',
  props: {
    offset: { required: true, type: Number }
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
        this.playerStore.isStopped ||
        this.queueStore.current.data_kind === 'pipe'
      )
    },
    visible() {
      return ['podcast', 'audiobook'].includes(
        this.queueStore.current.media_kind
      )
    }
  },
  methods: {
    seek() {
      player.seek(this.offset * -1)
    }
  }
}
</script>
