<template>
  <a v-on:click="toggle_consume_mode" v-bind:class="{ 'is-warning': is_consume }">
    <span class="icon"><i class="mdi mdi-fire"></i></span>
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonConsume',

  computed: {
    is_consume () {
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

<style>
</style>
