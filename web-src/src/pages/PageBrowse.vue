<template>
  <div>
    <tabs-music></tabs-music>

    <!-- Recently added -->
    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Recently added</p>
        <p class="heading">albums</p>
      </template>
      <template slot="content">
        <list-item-album v-for="album in recently_added.items" :key="album.id" :album="album" @click="open_album(album)">
          <template slot="actions">
            <a @click="open_album_dialog(album)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-album>
        <modal-dialog-album :show="show_album_details_modal" :album="selected_album" @close="show_album_details_modal = false" />
      </template>
      <template slot="footer">
        <nav class="level">
          <p class="level-item">
            <a class="button is-light is-small is-rounded" v-on:click="open_browse('recently_added')">Show more</a>
          </p>
        </nav>
      </template>
    </content-with-heading>

    <!-- Recently played -->
    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Recently played</p>
        <p class="heading">tracks</p>
      </template>
      <template slot="content">
        <list-item-track v-for="track in recently_played.items" :key="track.id" :track="track" @click="play_track(track)">
          <template slot="actions">
            <a @click="open_track_dialog(track)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-track>
        <modal-dialog-track :show="show_track_details_modal" :track="selected_track" @close="show_track_details_modal = false" />
      </template>
      <template slot="footer">
        <nav class="level">
          <p class="level-item">
            <a class="button is-light is-small is-rounded" v-on:click="open_browse('recently_played')">Show more</a>
          </p>
        </nav>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import ListItemAlbum from '@/components/ListItemAlbum'
import ListItemTrack from '@/components/ListItemTrack'
import ModalDialogTrack from '@/components/ModalDialogTrack'
import ModalDialogAlbum from '@/components/ModalDialogAlbum'
import webapi from '@/webapi'

const browseData = {
  load: function (to) {
    return Promise.all([
      webapi.search({ type: 'album', expression: 'time_added after 8 weeks ago and media_kind is music having track_count > 3 order by time_added desc', limit: 3 }),
      webapi.search({ type: 'track', expression: 'time_played after 8 weeks ago and media_kind is music order by time_played desc', limit: 3 })
    ])
  },

  set: function (vm, response) {
    vm.recently_added = response[0].data.albums
    vm.recently_played = response[1].data.tracks
  }
}

export default {
  name: 'PageBrowse',
  mixins: [ LoadDataBeforeEnterMixin(browseData) ],
  components: { ContentWithHeading, TabsMusic, ListItemAlbum, ListItemTrack, ModalDialogTrack, ModalDialogAlbum },

  data () {
    return {
      recently_added: {},
      recently_played: {},

      show_track_details_modal: false,
      selected_track: {},

      show_album_details_modal: false,
      selected_album: {}
    }
  },

  methods: {
    open_browse: function (type) {
      this.$router.push({ path: '/music/browse/' + type })
    },

    open_track_dialog: function (track) {
      this.selected_track = track
      this.show_track_details_modal = true
    },

    open_album: function (album) {
      this.$router.push({ path: '/music/albums/' + album.id })
    },

    open_album_dialog: function (album) {
      this.selected_album = album
      this.show_album_details_modal = true
    },

    play_track: function (track) {
      webapi.player_play_uri(track.uri, false)
    }
  }
}
</script>

<style>
</style>
