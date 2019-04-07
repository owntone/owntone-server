<template>
  <content-with-heading>
    <template slot="heading-left">
      <p class="title is-4">{{ name }}</p>
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
      <p class="heading has-text-centered-mobile">{{ albums.total }} albums | <a class="has-text-link" @click="open_tracks">{{ track_count }} tracks</a></p>
      <list-item-album v-for="album in albums.items" :key="album.id" :album="album" @click="open_album(album)">
        <template slot="actions">
          <a @click="open_dialog(album)">
            <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
          </a>
        </template>
      </list-item-album>
      <modal-dialog-album :show="show_details_modal" :album="selected_album" @close="show_details_modal = false" />
      <modal-dialog-artist :show="show_artist_details_modal" :artist="consolidated_artist" @close="show_artist_details_modal = false" />
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
    vm.name = response[0].data.name
    vm.id = response[0].data.id
    vm.artist = response[0].data.items
    vm.albums = response[1].data

    vm.consolidated_artist = {
      'id': vm.id,
      'name': vm.name,
      'album_count': vm.albums.items.length,
      'track_count': vm.track_count,
      'uri': vm.albums.items.map(a => a.uri).join(',')
    }
  }
}

export default {
  name: 'PageArtist',
  mixins: [ LoadDataBeforeEnterMixin(artistData) ],
  components: { ContentWithHeading, ListItemAlbum, ModalDialogAlbum, ModalDialogArtist },

  data () {
    return {
      name: '',
      id: '',
      consolidated_artist: {},
      artist: [], // can be multiple entries if compilation album
      albums: { items: [] },

      show_details_modal: false,
      selected_album: {},

      show_artist_details_modal: false
    }
  },

  computed: {
    track_count () {
      var n = 0
      return this.albums.items.reduce((acc, item) => {
        acc += item.track_count
        return acc
      }, n)
    }
  },

  methods: {
    open_tracks: function () {
      this.$router.push({ path: '/music/artists/' + this.id + '/tracks' })
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
