<template>
  <div>
    <content-with-heading>
      <template slot="options">
        <index-button-list :index="index_list"></index-button-list>
      </template>
      <template slot="heading-left">
        <p class="title is-4">{{ genre }}</p>
      </template>
      <template slot="heading-right">
        <div class="buttons is-centered">
          <a class="button is-small is-light is-rounded" @click="show_genre_details_modal = true">
            <span class="icon"><i class="mdi mdi-dots-horizontal mdi-18px"></i></span>
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
          </a>
        </div>
      </template>
      <template slot="content">
        <p class="heading has-text-centered-mobile">{{ artists.total }} artists | <a class="has-text-link" @click="open_albums">albums</a> | <a class="has-text-link" @click="open_tracks">tracks</a></p>
        <list-item-artist v-for="artist in artists.items" :key="artist.id" :artist="artist" @click="open_artist(artist)">
          <template slot="actions">
            <a @click="open_dialog(artist)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-artist>
        <modal-dialog-artist :show="show_details_modal" :artist="selected_artist" @close="show_details_modal = false" />
        <modal-dialog-genre :show="show_genre_details_modal" :genre="{ 'name': genre }" @close="show_genre_details_modal = false" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import IndexButtonList from '@/components/IndexButtonList'
import ListItemArtist from '@/components/ListItemArtist'
import ModalDialogArtist from '@/components/ModalDialogArtist'
import ModalDialogGenre from '@/components/ModalDialogGenre'
import webapi from '@/webapi'

const artistsData = {
  load: function (to) {
    return webapi.library_genre_artists(to.params.genre)
  },

  set: function (vm, response) {
    vm.genre = vm.$route.params.genre
    vm.artists = response.data.artists
  }
}

export default {
  name: 'PageGenreArtists',
  mixins: [ LoadDataBeforeEnterMixin(artistsData) ],
  components: { ContentWithHeading, ListItemArtist, IndexButtonList, ModalDialogArtist, ModalDialogGenre },

  data () {
    return {
      artists: { items: [] },
      genre: '',

      show_details_modal: false,
      selected_artist: {},

      show_genre_details_modal: false
    }
  },

  computed: {
    index_list () {
      return [...new Set(this.artists.items
        .map(artist => artist.name_sort.charAt(0).toUpperCase()))]
    }
  },

  methods: {
    open_albums: function () {
      this.show_details_modal = false
      this.$router.push({ name: 'Genre', params: { genre: this.genre } })
    },

    open_tracks: function () {
      this.show_details_modal = false
      this.$router.push({ name: 'GenreTracks', params: { genre: this.genre } })
    },

    open_artist: function (artist) {
      this.$router.push({ path: '/music/artists/' + artist.id })
    },

    play: function () {
      webapi.player_play_expression('genre is "' + this.genre + '" and media_kind is music', true)
    },

    play_artist: function (position) {
      webapi.player_play_expression('genre is "' + this.genre + '" and media_kind is music', false, position)
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
