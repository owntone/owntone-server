<template functional>
  <div class="media" :id="'index_' + props.album.name_sort.charAt(0).toUpperCase()">
    <div class="media-left fd-has-action"
        v-if="$slots['artwork']"
        @click="listeners.click">
      <slot name="artwork"></slot>
    </div>
    <div class="media-content fd-has-action is-clipped" @click="listeners.click">
      <div style="margin-top:0.7rem;">
        <h1 class="title is-6">{{ props.album.name }}</h1>
        <h2 class="subtitle is-7 has-text-grey"><b>{{ props.album.artist }}</b></h2>
        <h2 class="subtitle is-7 has-text-grey has-text-weight-normal"
            v-if="props.album.date_released && props.album.media_kind === 'music'">
          {{ props.album.date_released | time('L') }}
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
