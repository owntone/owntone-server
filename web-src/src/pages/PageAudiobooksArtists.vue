<template>
  <div>
    <tabs-audiobooks></tabs-audiobooks>

    <content-with-heading>
      <template slot="options">
        <index-button-list :index="index_list"></index-button-list>
      </template>
      <template slot="heading-left">
        <p class="title is-4">Authors</p>
        <p class="heading">{{ artists.total }} authors</p>
      </template>
      <template slot="heading-right">
      </template>
      <template slot="content">
        <list-artists :artists="artists.items"></list-artists>
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
    index_list () {
      return [...new Set(this.artists.items
        .filter(artist => !this.$store.state.hide_singles || artist.track_count > (artist.album_count * 2))
        .map(artist => artist.name_sort.charAt(0).toUpperCase()))]
    }
  },

  methods: {
  }
}
</script>

<style>
</style>
