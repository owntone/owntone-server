<template>
  <a v-on:click="toggle_shuffle_mode" v-bind:class="{ 'is-warning': is_shuffle }">
    <span class="icon"><i class="mdi" v-bind:class="{ 'mdi-shuffle': is_shuffle, 'mdi-shuffle-disabled': !is_shuffle }"></i></span>
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonShuffle',

  computed: {
    is_shuffle () {
      return this.$store.state.player.shuffle
    }
  },

  methods: {
    toggle_shuffle_mode: function () {
      webapi.player_shuffle(!this.is_shuffle)
    }
  }
}
</script>

<style>
</style>
