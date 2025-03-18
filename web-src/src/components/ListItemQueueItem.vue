<template>
  <div
    v-if="isNext || !hideReadItems"
    class="media is-align-items-center is-clickable mb-0"
    @click="play"
  >
    <div v-if="editing" class="media-left">
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
          'has-text-primary': isCurrent,
          'has-text-grey-light': !isNext
        }"
        v-text="item.title"
      />
      <div
        class="is-size-7 has-text-weight-bold"
        :class="{
          'has-text-primary': isCurrent,
          'has-text-grey-light': !isNext,
          'has-text-grey': isNext && !isCurrent
        }"
        v-text="item.artist"
      />
      <div
        class="is-size-7"
        :class="{
          'has-text-primary': isCurrent,
          'has-text-grey-light': !isNext,
          'has-text-grey': isNext && !isCurrent
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
    currentPosition: { required: true, type: Number },
    editing: Boolean,
    item: { required: true, type: Object },
    position: { required: true, type: Number },
    hideReadItems: Boolean
  },
  setup() {
    return { playerStore: usePlayerStore() }
  },
  computed: {
    isCurrent() {
      return this.item.id === this.playerStore.item_id
    },
    isNext() {
      return this.currentPosition < 0 || this.position >= this.currentPosition
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
