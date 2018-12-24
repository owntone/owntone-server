<template functional>
  <div class="media">
    <div class="media-content fd-has-action is-clipped" @click="listeners.click">
      <h1 class="title is-6">{{ props.playlist.name }}</h1>
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
