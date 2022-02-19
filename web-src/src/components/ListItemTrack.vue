<template>
  <div class="media" :id="'index_' + track.title_sort.charAt(0).toUpperCase()" :class="{ 'with-progress': $slots.progress }">
    <figure class="media-left fd-has-action" v-if="$slots.icon">
      <slot name="icon"></slot>
    </figure>
    <div class="media-content fd-has-action is-clipped">
      <h1 class="title is-6" :class="{ 'has-text-grey': track.media_kind === 'podcast' && track.play_count > 0 }">{{ track.title }}</h1>
      <h2 class="subtitle is-7 has-text-grey"><b>{{ track.artist }}</b></h2>
      <h2 class="subtitle is-7 has-text-grey">{{ track.album }}</h2>
      <slot name="progress"></slot>
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
