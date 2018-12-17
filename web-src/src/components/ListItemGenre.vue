<template functional>
  <div class="media" :id="'index_' + props.anchor">
    <div class="media-content fd-has-action is-clipped" @click="listeners.click">
      <h1 class="title is-6">{{ props.genre.name }}</h1>
    </div>
    <div class="media-right">
      <slot name="actions"></slot>
    </div>
  </div>
</template>

<script>
export default {
  name: 'ListItemGenre',
  props: [ 'genre', 'anchor' ]
}
</script>

<style>
</style>
