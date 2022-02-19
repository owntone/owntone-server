<template>
  <div class="media">
    <figure class="media-left fd-has-action" v-if="$slots.icon">
      <slot name="icon"></slot>
    </figure>
    <div class="media-content fd-has-action is-clipped">
      <h1 class="title is-6">{{ playlist.name }}</h1>
    </div>
    <div class="media-right">
      <slot name="actions"></slot>
    </div>
  </div>
</template>

<script>
export default {
  name: 'ListItemPlaylist',
  props: ['playlist']
}
</script>

<style>
</style>
