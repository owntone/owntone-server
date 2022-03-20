<template>
  <div class="media">
    <div v-if="$slots['artwork']" class="media-left fd-has-action">
      <slot name="artwork" />
    </div>
    <div class="media-content fd-has-action is-clipped">
      <h1 class="title is-6">
        {{ album.name }}
      </h1>
      <h2 class="subtitle is-7 has-text-grey">
        <b>{{ album.artists[0].name }}</b>
      </h2>
      <h2 class="subtitle is-7 has-text-grey has-text-weight-normal">
        ({{ album.album_type }}, {{ $filters.time(album.release_date, 'L') }})
      </h2>
    </div>
    <div class="media-right">
      <slot name="actions" />
    </div>
  </div>
</template>

<script>
export default {
  name: 'SpotifyListItemAlbum',
  props: ['album']
}
</script>

<style></style>
