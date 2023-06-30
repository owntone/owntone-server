<template>
  <a :class="{ 'is-info': is_shuffle }" @click="toggle_shuffle_mode">
    <mdicon
      class="icon"
      :name="icon_name"
      :size="icon_size"
      :title="$t('player.button.' + icon_name)"
    />
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonShuffle',

  props: {
    icon_size: {
      type: Number,
      default: 16
    }
  },

  computed: {
    is_shuffle() {
      return this.$store.state.player.shuffle
    },
    icon_name() {
      if (this.is_shuffle) {
        return 'shuffle'
      }
      return 'shuffle-disabled'
    }
  },

  methods: {
    toggle_shuffle_mode() {
      webapi.player_shuffle(!this.is_shuffle)
    }
  }
}
</script>

<style></style>
