<template>
  <div>
    <tabs-audiobooks></tabs-audiobooks>

    <content-with-heading>
      <template slot="options">
        <index-button-list :index="albums_list.indexList"></index-button-list>
      </template>
      <template slot="heading-left">
        <p class="title is-4">Audiobooks</p>
        <p class="heading">{{ albums_list.sortedAndFiltered.length }} Audiobooks</p>
      </template>
      <template slot="content">
        <list-albums :albums="albums_list"></list-albums>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import TabsAudiobooks from '@/components/TabsAudiobooks'
import IndexButtonList from '@/components/IndexButtonList'
import ContentWithHeading from '@/templates/ContentWithHeading'
import ListAlbums from '@/components/ListAlbums'
import webapi from '@/webapi'
import Albums from '@/lib/Albums'

const albumsData = {
  load: function (to) {
    return webapi.library_albums('audiobook')
  },

  set: function (vm, response) {
    vm.albums = response.data
  }
}

export default {
  name: 'PageAudiobooksAlbums',
  mixins: [LoadDataBeforeEnterMixin(albumsData)],
  components: { TabsAudiobooks, ContentWithHeading, IndexButtonList, ListAlbums },

  data () {
    return {
      albums: { items: [] }
    }
  },

  computed: {
    albums_list () {
      return new Albums(this.albums.items, {
        sort: 'Name',
        group: true
      })
    }
  },

  methods: {
  }
}
</script>

<style>
</style>
