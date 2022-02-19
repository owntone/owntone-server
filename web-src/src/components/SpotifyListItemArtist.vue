<template>
  <div class="media">
    <div class="media-content fd-has-action is-clipped" @click="open_artist">
      <h1 class="title is-6">
        {{ artist.name }}
      </h1>
    </div>
    <div class="media-right">
      <slot name="actions" />
    </div>
  </div>
</template>

<script>
export default {
  name: 'SpotifyListItemArtist',
  props: ['artist'],

  methods: {
    open_artist: function () {
      this.$router.push({ path: '/music/spotify/artists/' + this.artist.id })
    }
  }
}
</script>

<style></style>
