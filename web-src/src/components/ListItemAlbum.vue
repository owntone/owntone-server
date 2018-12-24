<template functional>
  <div class="media" :id="'index_' + props.album.name_sort.charAt(0).toUpperCase()">
    <div class="media-content fd-has-action is-clipped" @click="listeners.click">
      <h1 class="title is-6">{{ props.album.name }}</h1>
      <h2 class="subtitle is-7 has-text-grey"><b>{{ props.album.artist }}</b></h2>
    </div>
    <div class="media-right">
      <slot name="actions"></slot>
    </div>
  </div>
</template>

<script>
export default {
  name: 'ListItemAlbum',
  props: ['album', 'media_kind']
}
</script>

<style>
</style>
