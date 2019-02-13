<template>
  <div>
    <content-with-heading v-if="new_episodes.items.length > 0">
      <template slot="heading-left">
        <p class="title is-4">New episodes</p>
      </template>
      <template slot="content">
        <list-item-track v-for="track in new_episodes.items" :key="track.id" :track="track" @click="play_track(track)">
          <template slot="progress">
            <range-slider
              class="track-progress"
              min="0"
              :max="track.length_ms"
              step="1"
              :disabled="true"
              :value="track.seek_ms" >
            </range-slider>
          </template>
          <template slot="actions">
            <a @click="open_track_dialog(track)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-track>
        <modal-dialog-track :show="show_track_details_modal" :track="selected_track" @close="show_track_details_modal = false" @play_count_changed="reload_new_episodes" />
      </template>
    </content-with-heading>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Podcasts</p>
        <p class="heading">{{ albums.total }} podcasts</p>
      </template>
      <template slot="content">
        <list-item-album v-for="album in albums.items" :key="album.id" :album="album" :media_kind="'podcast'" @click="open_album(album)">
          <template slot="actions">
            <a @click="open_album_dialog(album)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-album>
        <modal-dialog-album :show="show_album_details_modal" :album="selected_album" :media_kind="'podcast'" @close="show_album_details_modal = false" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import ListItemTrack from '@/components/ListItemTrack'
import ListItemAlbum from '@/components/ListItemAlbum'
import ModalDialogTrack from '@/components/ModalDialogTrack'
import ModalDialogAlbum from '@/components/ModalDialogAlbum'
import RangeSlider from 'vue-range-slider'
import webapi from '@/webapi'

const albumsData = {
  load: function (to) {
    return Promise.all([
      webapi.library_podcasts(),
      webapi.library_podcasts_new_episodes()
    ])
  },

  set: function (vm, response) {
    vm.albums = response[0].data
    vm.new_episodes = response[1].data.tracks
  }
}

export default {
  name: 'PagePodcasts',
  mixins: [ LoadDataBeforeEnterMixin(albumsData) ],
  components: { ContentWithHeading, ListItemTrack, ListItemAlbum, ModalDialogTrack, ModalDialogAlbum, RangeSlider },

  data () {
    return {
      albums: {},
      new_episodes: { items: [] },

      show_album_details_modal: false,
      selected_album: {},

      show_track_details_modal: false,
      selected_track: {}
    }
  },

  methods: {
    open_album: function (album) {
      this.$router.push({ path: '/podcasts/' + album.id })
    },

    play_track: function (track) {
      webapi.player_play_uri(track.uri, false)
    },

    open_track_dialog: function (track) {
      this.selected_track = track
      this.show_track_details_modal = true
    },

    open_album_dialog: function (album) {
      this.selected_album = album
      this.show_album_details_modal = true
    },

    reload_new_episodes: function () {
      webapi.library_podcasts_new_episodes().then(({ data }) => {
        this.new_episodes = data.tracks
      })
    }
  }
}
</script>

<style>
</style>
