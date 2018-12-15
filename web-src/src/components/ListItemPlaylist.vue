<template>
  <div class="media">
    <div class="media-content fd-has-action is-clipped" v-on:click="open_playlist">
      <h1 class="title is-6">{{ playlist.name }}</h1>
    </div>
    <div class="media-right">
      <slot name="actions"></slot>
    </div>
  </div>
</template>

<script>
export default {
  name: 'ListItemPlaylist',
  components: {},

  props: ['playlist'],

  methods: {
    open_playlist: function () {
      this.show_details_modal = false
      this.$router.push({ path: '/playlists/' + this.playlist.id })
    }
  }
}
</script>

<style>
</style>
