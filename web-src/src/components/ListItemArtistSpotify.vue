<template>
  <div class="media is-align-items-center">
    <div class="media-content is-clickable is-clipped" @click="open_artist">
      <h1 class="title is-6" v-text="artist.name" />
    </div>
    <div class="media-right">
      <slot name="actions" />
    </div>
  </div>
</template>

<script>
export default {
  name: 'ListItemArtistSpotify',
  props: ['artist'],

  methods: {
    open_artist() {
      this.$router.push({
        name: 'music-spotify-artist',
        params: { id: this.artist.id }
      })
    }
  }
}
</script>

<style></style>
