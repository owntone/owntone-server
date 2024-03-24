<template>
  <div class="media is-align-items-center" @click="open_album">
    <div v-if="$slots['artwork']" class="media-left is-clickable">
      <slot name="artwork" />
    </div>
    <div class="media-content is-clickable is-clipped">
      <h1 class="title is-6" v-text="item.name" />
      <h2
        class="subtitle is-7 has-text-grey has-text-weight-bold"
        v-text="item.artists[0]?.name"
      />
      <h2
        class="subtitle is-7 has-text-grey"
        v-text="[item.album_type, $filters.date(item.release_date)].join(', ')"
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
  props: { item: { required: true, type: Object } },

  methods: {
    open_album() {
      this.$router.push({
        name: 'music-spotify-album',
        params: { id: this.item.id }
      })
    }
  }
}
</script>

<style></style>
