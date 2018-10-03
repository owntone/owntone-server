<template>
  <div>
    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Audiobooks</p>
        <p class="heading">{{ albums.total }} audiobooks</p>
      </template>
      <template slot="content">
        <list-item-album v-for="album in albums.items" :key="album.id" :album="album" :media_kind="'audiobook'"></list-item-album>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import ListItemAlbum from '@/components/ListItemAlbum'
import webapi from '@/webapi'

const albumsData = {
  load: function (to) {
    return webapi.library_audiobooks()
  },

  set: function (vm, response) {
    vm.albums = response.data
  }
}

export default {
  name: 'PageAudiobooks',
  mixins: [ LoadDataBeforeEnterMixin(albumsData) ],
  components: { ContentWithHeading, ListItemAlbum },

  data () {
    return {
      albums: {}
    }
  }
}
</script>

<style>
</style>
