<template>
  <a v-if="visible" :disabled="disabled" @click="seek">
    <mdicon class="icon" name="rewind" :size="icon_size" />
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonSeekBack',
  props: {
    seek_ms: Number,
    icon_size: {
      type: Number,
      default: 16
    }
  },

  computed: {
    now_playing() {
      return this.$store.getters.now_playing
    },
    is_stopped() {
      return this.$store.state.player.state === 'stop'
    },
    disabled() {
      return (
        !this.$store.state.queue ||
        this.$store.state.queue.count <= 0 ||
        this.is_stopped ||
        this.now_playing.data_kind === 'pipe'
      )
    },
    visible() {
      return ['podcast', 'audiobook'].includes(this.now_playing.media_kind)
    }
  },

  methods: {
    seek: function () {
      if (!this.disabled) {
        webapi.player_seek(this.seek_ms * -1)
      }
    }
  }
}
</script>
