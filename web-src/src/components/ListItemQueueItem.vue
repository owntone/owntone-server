<template>
  <div
    v-if="is_next || !show_only_next_items"
    class="media is-align-items-center"
  >
    <div v-if="edit_mode" class="media-left">
      <mdicon
        class="icon has-text-grey fd-is-movable handle"
        name="drag-horizontal"
        size="16"
      />
    </div>
    <div class="media-content is-clickable is-clipped" @click="play">
      <h1
        class="title is-6"
        :class="{
          'has-text-primary': item.id === state.item_id,
          'has-text-grey-light': !is_next
        }"
        v-text="item.title"
      />
      <h2
        class="subtitle is-7 has-text-weight-bold"
        :class="{
          'has-text-primary': item.id === state.item_id,
          'has-text-grey-light': !is_next,
          'has-text-grey': is_next && item.id !== state.item_id
        }"
        v-text="item.artist"
      />
      <h2
        class="subtitle is-7"
        :class="{
          'has-text-primary': item.id === state.item_id,
          'has-text-grey-light': !is_next,
          'has-text-grey': is_next && item.id !== state.item_id
        }"
        v-text="item.album"
      />
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
  props: {
    current_position: { required: true, type: Number },
    edit_mode: Boolean,
    item: { required: true, type: Object },
    position: { required: true, type: Number },
    show_only_next_items: Boolean
  },

  computed: {
    is_next() {
      return this.current_position < 0 || this.position >= this.current_position
    },
    state() {
      return this.$store.state.player
    }
  },

  methods: {
    play() {
      webapi.player_play({ item_id: this.item.id })
    }
  }
}
</script>

<style></style>
