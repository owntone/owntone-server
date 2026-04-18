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

<script setup>
import { computed } from 'vue'
import player from '@/api/player'
import { usePlayerStore } from '@/stores/player'

const props = defineProps({
  currentPosition: { required: true, type: Number },
  editing: Boolean,
  hideReadItems: Boolean,
  item: { required: true, type: Object },
  position: { required: true, type: Number }
})

const playerStore = usePlayerStore()

const isCurrent = computed(() => props.item.id === playerStore.item_id)
const isNext = computed(
  () => props.currentPosition < 0 || props.position >= props.currentPosition
)

const play = () => {
  player.play({ item_id: props.item.id })
}
</script>

<style scoped>
.is-movable {
  cursor: move;
}
</style>
