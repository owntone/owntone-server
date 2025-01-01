<template>
  <div
    v-if="is_next || !show_only_next_items"
    class="media is-align-items-center is-clickable mb-0"
    @click="play"
  >
    <div v-if="edit_mode" class="media-left">
      <mdicon
        class="icon has-text-grey is-movable"
        name="drag-horizontal"
        size="18"
      />
    </div>
    <div class="media-content">
      <div
        class="is-size-6 has-text-weight-bold"
        :class="{
          'has-text-primary': item.id === player.item_id,
          'has-text-grey-light': !is_next
        }"
        v-text="item.title"
      />
      <div
        class="is-size-7 has-text-weight-bold"
        :class="{
          'has-text-primary': item.id === player.item_id,
          'has-text-grey-light': !is_next,
          'has-text-grey': is_next && item.id !== player.item_id
        }"
        v-text="item.artist"
      />
      <div
        class="is-size-7"
        :class="{
          'has-text-primary': item.id === player.item_id,
          'has-text-grey-light': !is_next,
          'has-text-grey': is_next && item.id !== player.item_id
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
import { usePlayerStore } from '@/stores/player'
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

  setup() {
    return {
      playerStore: usePlayerStore()
    }
  },

  computed: {
    is_next() {
      return this.current_position < 0 || this.position >= this.current_position
    },
    player() {
      return this.playerStore
    }
  },

  methods: {
    play() {
      webapi.player_play({ item_id: this.item.id })
    }
  }
}
</script>

<style scoped>
.is-movable {
  cursor: move;
}
</style>
