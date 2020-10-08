<template>
  <div>
    <tabs-audiobooks></tabs-audiobooks>

    <content-with-heading>
      <template slot="options">
        <index-button-list :index="artists_list.indexList"></index-button-list>
      </template>
      <template slot="heading-left">
        <p class="title is-4">Authors</p>
        <p class="heading">{{ artists_list.sortedAndFiltered.length }} Authors</p>
      </template>
      <template slot="heading-right">
      </template>
      <template slot="content">
        <list-artists :artists="artists_list"></list-artists>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsAudiobooks from '@/components/TabsAudiobooks'
import IndexButtonList from '@/components/IndexButtonList'
import ListArtists from '@/components/ListArtists'
import webapi from '@/webapi'
import Artists from '@/lib/Artists'

const artistsData = {
  load: function (to) {
    return webapi.library_artists('audiobook')
  },

  set: function (vm, response) {
    vm.artists = response.data
  }
}

export default {
  name: 'PageAudiobooksArtists',
  mixins: [LoadDataBeforeEnterMixin(artistsData)],
  components: { ContentWithHeading, TabsAudiobooks, IndexButtonList, ListArtists },

  data () {
    return {
      artists: { items: [] }
    }
  },

  computed: {
    artists_list () {
      return new Artists(this.artists.items, {
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
