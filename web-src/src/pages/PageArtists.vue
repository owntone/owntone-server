<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Artists</p>
        <p class="heading">{{ artists.total }} artists</p>
      </template>
      <template slot="heading-right">
        <a class="button is-small" :class="{ 'is-info': hide_singles }" @click="update_hide_singles">
          <span class="icon">
            <i class="mdi mdi-numeric-1-box-multiple-outline"></i>
          </span>
          <span>Hide singles</span>
        </a>
      </template>
      <template slot="content">
        <list-item-artist v-for="artist in artists.items" :key="artist.id" :artist="artist" v-if="!hide_singles || artist.track_count > (artist.album_count * 2)"></list-item-artist>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import ListItemArtist from '@/components/ListItemArtist'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'

const artistsData = {
  load: function (to) {
    return webapi.library_artists()
  },

  set: function (vm, response) {
    vm.artists = response.data
  }
}

export default {
  name: 'PageArtists',
  mixins: [ LoadDataBeforeEnterMixin(artistsData) ],
  components: { ContentWithHeading, TabsMusic, ListItemArtist },

  data () {
    return {
      artists: {}
    }
  },

  computed: {
    hide_singles () {
      return this.$store.state.hide_singles
    }
  },

  methods: {
    update_hide_singles: function (e) {
      this.$store.commit(types.HIDE_SINGLES, !this.hide_singles)
    }
  }
}
</script>

<style>
</style>
