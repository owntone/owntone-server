<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="options">
        <index-button-list :index="index_list"></index-button-list>
      </template>
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
        <list-item-artist v-for="artist in artists.items"
          :key="artist.id"
          :artist="artist"
          @click="open_artist(artist)"
          v-if="!hide_singles || artist.track_count > (artist.album_count * 2)">
            <template slot="actions">
              <a @click="open_dialog(artist)">
                <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
              </a>
            </template>
        </list-item-artist>
        <modal-dialog-artist :show="show_details_modal" :artist="selected_artist" @close="show_details_modal = false" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import IndexButtonList from '@/components/IndexButtonList'
import ListItemArtist from '@/components/ListItemArtist'
import ModalDialogArtist from '@/components/ModalDialogArtist'
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
  components: { ContentWithHeading, TabsMusic, IndexButtonList, ListItemArtist, ModalDialogArtist },

  data () {
    return {
      artists: { items: [] },

      show_details_modal: false,
      selected_artist: {}
    }
  },

  computed: {
    hide_singles () {
      return this.$store.state.hide_singles
    },

    index_list () {
      return [...new Set(this.artists.items
        .filter(artist => !this.$store.state.hide_singles || artist.track_count > (artist.album_count * 2))
        .map(artist => artist.name_sort.charAt(0).toUpperCase()))]
    }
  },

  methods: {
    update_hide_singles: function (e) {
      this.$store.commit(types.HIDE_SINGLES, !this.hide_singles)
    },

    open_artist: function (artist) {
      this.$router.push({ path: '/music/artists/' + artist.id })
    },

    open_dialog: function (artist) {
      this.selected_artist = artist
      this.show_details_modal = true
    }
  }
}
</script>

<style>
</style>
