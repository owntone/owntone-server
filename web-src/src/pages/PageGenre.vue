<template>
  <div>
    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">{{ name }}</p>
      </template>
      <template slot="heading-right">
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
        </a>
      </template>
      <template slot="content">
        <p class="heading has-text-centered-mobile">{{ genreAlbums.total }} albums | <a class="has-text-link" @click="open_tracks">tracks</a></p>
        <list-item-albums v-for="album in genreAlbums.items" :key="album.id" :album="album" :links="links" @click="open_album(album)">
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
import ListItemAlbums from '@/components/ListItemAlbum'
import ModalDialogAlbum from '@/components/ModalDialogAlbum'
import webapi from '@/webapi'

const genreData = {
  load: function (to) {
    return webapi.library_genre(to.params.genre)
  },

  set: function (vm, response) {
    vm.name = vm.$route.params.genre
    vm.genreAlbums = response.data.albums
    var li = 0
    var v = null
    var i
    for (i = 0; i < vm.genreAlbums.items.length; i++) {
      var n = vm.genreAlbums.items[i].name_sort.charAt(0).toUpperCase()
      if (n !== v) {
        var obj = {}
        obj.n = n
        obj.a = 'idx_nav_' + li
        vm.links.push(obj)
        li++
        v = n
      }
    }
  }
}

export default {
  name: 'PageGenre',
  mixins: [ LoadDataBeforeEnterMixin(genreData) ],
  components: { ContentWithHeading, TabsMusic, ListItemAlbums, ModalDialogAlbum },

  data () {
    return {
      name: '',
      genreAlbums: {},
      links: [],

      show_details_modal: false,
      selected_album: {}
    }
  },

  methods: {
    open_tracks: function () {
      this.show_details_modal = false
      this.$router.push({ path: '/music/genres/' + this.name + '/tracks' })
    },

    play: function () {
      webapi.player_play_uri(this.genreAlbums.items.map(a => a.uri).join(','), true)
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
