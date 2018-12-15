<template>
  <div class="media" :id="'index_' + anchor">
    <div class="media-content fd-has-action is-clipped" v-on:click="open_album">
      <h1 class="title is-6">{{ album.name }}</h1>
      <h2 class="subtitle is-7 has-text-grey"><b>{{ album.artist }}</b></h2>
    </div>
    <div class="media-right">
      <slot name="actions"></slot>
    </div>
  </div>
</template>

<script>
export default {
  name: 'ListItemAlbum',
  components: {},

  props: ['album', 'media_kind', 'anchor'],

  data () {
    return {
      show_details_modal: false
    }
  },

  methods: {
    open_album: function () {
      this.show_details_modal = false
      if (this.media_kind === 'podcast') {
        this.$router.push({ path: '/podcasts/' + this.album.id })
      } else if (this.media_kind === 'audiobook') {
        this.$router.push({ path: '/audiobooks/' + this.album.id })
      } else {
        this.$router.push({ path: '/music/albums/' + this.album.id })
      }
    }
  }
}
</script>

<style>
</style>
