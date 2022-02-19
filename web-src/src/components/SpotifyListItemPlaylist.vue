<template>
  <div class="media">
    <div class="media-content fd-has-action is-clipped" @click="open_playlist">
      <h1 class="title is-6">
        {{ playlist.name }}
      </h1>
      <h2 class="subtitle is-7">
        {{ playlist.owner.display_name }}
      </h2>
    </div>
    <div class="media-right">
      <slot name="actions" />
    </div>
  </div>
</template>

<script>
export default {
  name: 'SpotifyListItemPlaylist',
  props: ['playlist'],

  methods: {
    open_playlist: function () {
      this.$router.push({
        path: '/music/spotify/playlists/' + this.playlist.id
      })
    }
  }
}
</script>

<style></style>
