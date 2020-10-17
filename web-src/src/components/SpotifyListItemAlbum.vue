<template functional>
  <div class="media">
    <div class="media-left fd-has-action"
        v-if="$slots['artwork']"
        @click="listeners.click">
      <slot name="artwork"></slot>
    </div>
    <div class="media-content fd-has-action is-clipped" @click="listeners.click">
      <h1 class="title is-6">{{ props.album.name }}</h1>
      <h2 class="subtitle is-7 has-text-grey"><b>{{ props.album.artists[0].name }}</b></h2>
      <h2 class="subtitle is-7 has-text-grey has-text-weight-normal">({{ props.album.album_type }}, {{ props.album.release_date | time('L') }})</h2>
    </div>
    <div class="media-right">
      <slot name="actions"></slot>
    </div>
  </div>
</template>

<script>
export default {
  name: 'SpotifyListItemAlbum',
  props: ['album']
}
</script>

<style>
</style>
