<template>
  <div class="media is-align-items-center">
    <div v-if="$slots['artwork']" class="media-left is-clickable">
      <slot name="artwork" />
    </div>
    <div class="media-content is-clickable is-clipped">
      <h1 class="title is-6" v-text="album.name" />
      <h2 class="subtitle is-7 has-text-grey">
        <b v-text="album.artists[0].name" />
      </h2>
      <h2
        class="subtitle is-7 has-text-grey has-text-weight-normal"
        v-text="
          [album.album_type, $filters.date(album.release_date)].join(', ')
        "
      />
    </div>
    <div class="media-right">
      <slot name="actions" />
    </div>
  </div>
</template>

<script>
export default {
  name: 'ListItemAlbumSpotify',
  props: ['album']
}
</script>

<style></style>
