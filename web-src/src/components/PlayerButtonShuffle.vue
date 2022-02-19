<template>
  <a :class="{ 'is-warning': is_shuffle }" @click="toggle_shuffle_mode">
    <span class="icon"
      ><i
        class="mdi"
        :class="[
          icon_style,
          { 'mdi-shuffle': is_shuffle, 'mdi-shuffle-disabled': !is_shuffle }
        ]"
    /></span>
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonShuffle',

  props: {
    icon_style: String
  },

  computed: {
    is_shuffle() {
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

<style></style>
