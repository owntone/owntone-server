<template>
  <div class="media is-align-items-center is-clickable mb-0" @click="play">
    <slot name="icon" />
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
import player from '@/api/player'

const props = defineProps({
  isCurrent: Boolean,
  isNext: Boolean,
  item: { required: true, type: Object }
})

const play = () => {
  player.play({ item_id: props.item.id })
}
</script>
