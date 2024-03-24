<template>
  <div class="media is-align-items-center">
    <div class="media-content is-clickable is-clipped" @click="open_playlist">
      <h1 class="title is-6" v-text="item.name" />
      <h2 class="subtitle is-7" v-text="item.owner.display_name" />
    </div>
    <div class="media-right">
      <slot name="actions" />
    </div>
  </div>
</template>

<script>
export default {
  name: 'ListItemPlaylistSpotify',
  props: { item: { required: true, type: Object } },

  methods: {
    open_playlist() {
      this.$router.push({
        name: 'playlist-spotify',
        params: { id: this.item.id }
      })
    }
  }
}
</script>

<style></style>
