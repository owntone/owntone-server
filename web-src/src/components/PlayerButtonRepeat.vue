<template>
  <a :class="{ 'is-warning': !is_repeat_off }" @click="toggle_repeat_mode">
    <mdicon class="icon" :name="icon_name" :size="icon_size" />
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonRepeat',

  props: {
    icon_size: {
      type: Number,
      default: 16
    }
  },

  computed: {
    is_repeat_all() {
      return this.$store.state.player.repeat === 'all'
    },
    is_repeat_single() {
      return this.$store.state.player.repeat === 'single'
    },
    is_repeat_off() {
      return !this.is_repeat_all && !this.is_repeat_single
    },
    icon_name() {
      if (this.is_repeat_all) {
        return 'repeat'
      } else if (this.is_repeat_single) {
        return 'repeat-once'
      }
      return 'repeat-off'
    }
  },

  methods: {
    toggle_repeat_mode: function () {
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

<style></style>
