<template>
  <div>
    <content-with-heading>
      <template slot="options">
        <index-button-list :index="index_list"></index-button-list>
      </template>
      <template slot="heading-left">
        <p class="title is-4">{{ name }}</p>
      </template>
      <template slot="heading-right">
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
        </a>
      </template>
      <template slot="content">
        <p class="heading has-text-centered-mobile">{{ genre_albums.total }} albums | <a class="has-text-link" @click="open_tracks">tracks</a></p>
        <list-item-albums v-for="album in genre_albums.items" :key="album.id" :album="album" @click="open_album(album)">
          <template slot="actions">
            <a @click="open_dialog(album)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-albums>
        <modal-dialog-album :show="show_details_modal" :album="selected_album" @close="show_details_modal = false" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import IndexButtonList from '@/components/IndexButtonList'
import ListItemAlbums from '@/components/ListItemAlbum'
import ModalDialogAlbum from '@/components/ModalDialogAlbum'
import webapi from '@/webapi'

const genreData = {
  load: function (to) {
    return webapi.library_genre(to.params.genre)
  },

  set: function (vm, response) {
    vm.name = vm.$route.params.genre
    vm.genre_albums = response.data.albums
  }
}

export default {
  name: 'PageGenre',
  mixins: [ LoadDataBeforeEnterMixin(genreData) ],
  components: { ContentWithHeading, TabsMusic, IndexButtonList, ListItemAlbums, ModalDialogAlbum },

  data () {
    return {
      name: '',
      genre_albums: { items: [] },

      show_details_modal: false,
      selected_album: {}
    }
  },

  computed: {
    index_list () {
      return [...new Set(this.genre_albums.items
        .map(album => album.name.charAt(0).toUpperCase()))]
    }
  },

  methods: {
    open_tracks: function () {
      this.show_details_modal = false
      this.$router.push({ path: '/music/genres/' + this.name + '/tracks' })
    },

    play: function () {
      webapi.library_genre_tracks(this.name).then(({ data }) =>
        webapi.player_play_uri(data.tracks.items.map(a => a.uri).join(','), true)
      )
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
