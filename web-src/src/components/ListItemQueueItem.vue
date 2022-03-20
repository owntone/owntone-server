<template>
  <div v-if="is_next || !show_only_next_items" class="media">
    <div v-if="edit_mode" class="media-left">
      <span class="icon has-text-grey fd-is-movable handle"
        ><i class="mdi mdi-drag-horizontal mdi-18px"
      /></span>
    </div>

    <div class="media-content fd-has-action is-clipped" @click="play">
      <h1
        class="title is-6"
        :class="{
          'has-text-primary': item.id === state.item_id,
          'has-text-grey-light': !is_next
        }"
      >
        {{ item.title }}
      </h1>
      <h2
        class="subtitle is-7"
        :class="{
          'has-text-primary': item.id === state.item_id,
          'has-text-grey-light': !is_next,
          'has-text-grey': is_next && item.id !== state.item_id
        }"
      >
        <b>{{ item.artist }}</b>
      </h2>
      <h2
        class="subtitle is-7"
        :class="{
          'has-text-primary': item.id === state.item_id,
          'has-text-grey-light': !is_next,
          'has-text-grey': is_next && item.id !== state.item_id
        }"
      >
        {{ item.album }}
      </h2>
    </div>
    <div class="media-right">
      <slot name="actions" />
    </div>
  </div>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'ListItemQueueItem',
  props: [
    'item',
    'position',
    'current_position',
    'show_only_next_items',
    'edit_mode'
  ],

  computed: {
    state() {
      return this.$store.state.player
    },

    is_next() {
      return this.current_position < 0 || this.position >= this.current_position
    }
  },

  methods: {
    play: function () {
      webapi.player_play({ item_id: this.item.id })
    }
  }
}
</script>

<style></style>
