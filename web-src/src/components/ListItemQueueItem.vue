<template>
  <div
    v-if="is_next || !show_only_next_items"
    class="media is-align-items-center"
  >
    <div v-if="edit_mode" class="media-left">
      <mdicon
        class="icon has-text-grey is-movable"
        name="drag-horizontal"
        size="18"
      />
    </div>
    <div class="media-content is-clickable is-clipped" @click="play">
      <p
        class="title is-6"
        :class="{
          'has-text-primary': item.id === player.item_id,
          'has-text-grey-light': !is_next
        }"
        v-text="item.title"
      />
      <p
        class="subtitle is-7 has-text-weight-bold"
        :class="{
          'has-text-primary': item.id === player.item_id,
          'has-text-grey-light': !is_next,
          'has-text-grey': is_next && item.id !== player.item_id
        }"
        v-text="item.artist"
      />
      <p
        class="subtitle is-7"
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
