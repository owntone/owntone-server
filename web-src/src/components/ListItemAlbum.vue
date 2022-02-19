<template>
  <div class="media" :id="'index_' + album.name_sort.charAt(0).toUpperCase()">
    <div class="media-left fd-has-action"
        v-if="$slots['artwork']">
      <slot name="artwork"></slot>
    </div>
    <div class="media-content fd-has-action is-clipped">
      <div style="margin-top:0.7rem;">
        <h1 class="title is-6">{{ album.name }}</h1>
        <h2 class="subtitle is-7 has-text-grey"><b>{{ album.artist }}</b></h2>
        <h2 class="subtitle is-7 has-text-grey has-text-weight-normal"
            v-if="album.date_released && album.media_kind === 'music'">
          {{ $filters.time(album.date_released, 'L') }}
        </h2>
      </div>
    </div>
    <div class="media-right" style="padding-top:0.7rem;">
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
