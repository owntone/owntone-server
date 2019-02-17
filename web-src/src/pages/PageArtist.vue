<template>
  <content-with-heading>
    <template slot="heading-left">
      <p class="title is-4">{{ artist.name }}</p>
    </template>
    <template slot="heading-right">
      <div class="buttons is-centered">
        <a class="button is-small is-light is-rounded" @click="show_artist_details_modal = true">
          <span class="icon"><i class="mdi mdi-dots-horizontal mdi-18px"></i></span>
        </a>
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
        </a>
      </div>
    </template>
    <template slot="content">
      <p class="heading has-text-centered-mobile">{{ artist.album_count }} albums | <a class="has-text-link" @click="open_tracks">{{ artist.track_count }} tracks</a></p>
      <list-item-album v-for="album in albums.items" :key="album.id" :album="album" @click="open_album(album)">
        <template slot="actions">
          <a @click="open_dialog(album)">
            <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
          </a>
        </template>
      </list-item-album>
      <modal-dialog-album :show="show_details_modal" :album="selected_album" @close="show_details_modal = false" />
      <modal-dialog-artist :show="show_artist_details_modal" :artist="artist" @close="show_artist_details_modal = false" />
    </template>
  </content-with-heading>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import ListItemAlbum from '@/components/ListItemAlbum'
import ModalDialogAlbum from '@/components/ModalDialogAlbum'
import ModalDialogArtist from '@/components/ModalDialogArtist'
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
  components: { ContentWithHeading, ListItemAlbum, ModalDialogAlbum, ModalDialogArtist },

  data () {
    return {
      artist: {},
      albums: {},

      show_details_modal: false,
      selected_album: {},

      show_artist_details_modal: false
    }
  },

  methods: {
    open_tracks: function () {
      this.$router.push({ path: '/music/artists/' + this.artist.id + '/tracks' })
    },

    play: function () {
      webapi.player_play_uri(this.albums.items.map(a => a.uri).join(','), true)
    },

    open_album: function (album) {
      this.$router.push({ path: '/music/albums/' + album.id })
    },

    open_dialog: function (album) {
      this.selected_album = album
      this.show_details_modal = true
    }
  }
}
</script>

<style>
</style>
