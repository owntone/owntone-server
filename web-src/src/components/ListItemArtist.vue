<template functional>
  <div class="media">
    <div class="media-content fd-has-action is-clipped" @click="listeners.click">
      <h1 class="title is-6">{{ props.artist.name }}</h1>
    </div>
    <div class="media-right">
      <slot name="actions"></slot>
    </div>
  </div>
</template>

<script>
export default {
  name: 'ListItemArtist',
  props: ['artist']
}
</script>

<style>
</style>
