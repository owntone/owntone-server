<template>
  <div class="media">
    <div class="media-content fd-has-action is-clipped" v-on:click="open_album">
      <h1 class="title is-6">{{ album.name }}</h1>
      <h2 class="subtitle is-7 has-text-grey"><b>{{ album.artists[0].name }}</b></h2>
    </div>
    <div class="media-right">
      <slot name="actions"></slot>
    </div>
  </div>
</template>

<script>
export default {
  name: 'SpotifyListItemAlbum',

  props: ['album'],

  methods: {
    open_album: function () {
      this.$router.push({ path: '/music/spotify/albums/' + this.album.id })
    }
  }
}
</script>

<style>
</style>
