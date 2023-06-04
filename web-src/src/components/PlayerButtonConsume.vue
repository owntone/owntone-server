<template>
  <a :class="{ 'is-info': is_consume }" @click="toggle_consume_mode">
    <span class="icon"
      ><mdicon
        name="fire"
        :size="icon_size"
        :title="$t('player.button.consume')"
    /></span>
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonConsume',

  props: {
    icon_size: {
      type: Number,
      default: 16
    }
  },

  computed: {
    is_consume() {
      return this.$store.state.player.consume
    }
  },

  methods: {
    toggle_consume_mode: function () {
      webapi.player_consume(!this.is_consume)
    }
  }
}
</script>

<style></style>
