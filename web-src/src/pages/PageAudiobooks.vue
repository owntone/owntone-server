<template>
  <div>
    <tabs-audiobooks></tabs-audiobooks>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Audiobooks</p>
        <p class="heading">{{ albums.total }} audiobooks</p>
      </template>
      <template slot="content">
        <list-albums :albums="albums.items"></list-albums>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import TabsAudiobooks from '@/components/TabsAudiobooks'
import ContentWithHeading from '@/templates/ContentWithHeading'
import ListAlbums from '@/components/ListAlbums'
import webapi from '@/webapi'

const albumsData = {
  load: function (to) {
    return webapi.library_albums('audiobook')
  },

  set: function (vm, response) {
    vm.albums = response.data
  }
}

export default {
  name: 'PageAudiobooks',
  mixins: [LoadDataBeforeEnterMixin(albumsData)],
  components: { TabsAudiobooks, ContentWithHeading, ListAlbums },

  data () {
    return {
      albums: { items: [] }
    }
  },

  methods: {
  }
}
</script>

<style>
</style>
