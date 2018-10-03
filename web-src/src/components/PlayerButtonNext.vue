<template>
  <a v-on:click="play_next">
    <span class="icon"><i class="mdi mdi-skip-forward"></i></span>
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonNext',

  methods: {
    play_next: function () {
      webapi.player_next()
    }
  }
}
</script>

<style>
</style>
