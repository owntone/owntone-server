<template>
  <a v-on:click="toggle_repeat_mode" v-bind:class="{ 'is-warning': !is_repeat_off }">
    <span class="icon"><i class="mdi" v-bind:class="{ 'mdi-repeat': is_repeat_all, 'mdi-repeat-once': is_repeat_single, 'mdi-repeat-off': is_repeat_off }"></i></span>
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonRepeat',

  data () {
    return { }
  },

  computed: {
    is_repeat_all () {
      return this.$store.state.player.repeat === 'all'
    },
    is_repeat_single () {
      return this.$store.state.player.repeat === 'single'
    },
    is_repeat_off () {
      return !this.is_repeat_all && !this.is_repeat_single
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

<style>
</style>
