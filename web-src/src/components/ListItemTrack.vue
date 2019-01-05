<template functional>
  <div class="media" :id="'index_' + props.track.title_sort.charAt(0).toUpperCase()">
    <figure class="media-left fd-has-action" v-if="slots().icon" @click="listeners.click">
      <slot name="icon"></slot>
    </figure>
    <div class="media-content fd-has-action is-clipped" @click="listeners.click">
      <h1 class="title is-6">{{ props.track.title }}</h1>
      <h2 class="subtitle is-7 has-text-grey"><b>{{ props.track.artist }}</b></h2>
      <h2 class="subtitle is-7 has-text-grey">{{ props.track.album }}</h2>
    </div>
    <div class="media-right">
      <slot name="actions"></slot>
    </div>
  </div>
</template>

<script>
export default {
  name: 'ListItemTrack',
  props: ['track']
}
</script>

<style>
</style>
