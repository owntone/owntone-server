<template>
  <content-with-heading>
    <template slot="heading-left">
      <p class="title is-4">{{ artist.name }}</p>
    </template>
    <template slot="content">
      <p class="heading has-text-centered-mobile">{{ artist.album_count }} albums | {{ artist.track_count }} tracks</p>
      <list-item-album v-for="album in albums.items" :key="album.id" :album="album"></list-item-album>
    </template>
  </content-with-heading>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import ListItemAlbum from '@/components/ListItemAlbum'
import webapi from '@/webapi'

const artistData = {
  load: function (to) {
    return Promise.all([
      webapi.library_artist(to.params.artist_id),
      webapi.library_albums(to.params.artist_id)
    ])
  },

  set: function (vm, response) {
    vm.artist = response[0].data
    vm.albums = response[1].data
  }
}

export default {
  name: 'PageArtist',
  mixins: [ LoadDataBeforeEnterMixin(artistData) ],
  components: { ContentWithHeading, ListItemAlbum },

  data () {
    return {
      artist: {},
      albums: {}
    }
  },

  methods: {
  }
}
</script>

<style>
</style>
