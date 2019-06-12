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
          <star-rating v-model="min_rating"
            :star-size="17"
            :show-rating="false"
            :max-rating="5"
            :increment="0.5"
            @rating-selected="show_rating"></star-rating>
          <a class="button is-small is-light is-rounded" @click="show_genre_details_modal = true">
            <span class="icon"><i class="mdi mdi-dots-horizontal mdi-18px"></i></span>
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
          </a>
        </div>
      </template>
      <template slot="content">
        <p class="heading has-text-centered-mobile"><a class="has-text-link" @click="open_genre">albums</a> | {{ tracks.total }} tracks</p>
        <list-item-track v-for="(track, index) in tracks.items" :key="track.id" :track="track" v-if="track.rating >= min_rating" @click="play_track(index)">
          <template slot="actions">
            <a @click="open_dialog(track)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-track>
        <modal-dialog-track :show="show_details_modal" :track="selected_track" @close="show_details_modal = false" />
        <modal-dialog-genre :show="show_genre_details_modal" :genre="modal_obj" @close="show_genre_details_modal = false" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import IndexButtonList from '@/components/IndexButtonList'
import ListItemTrack from '@/components/ListItemTrack'
import ModalDialogTrack from '@/components/ModalDialogTrack'
import ModalDialogGenre from '@/components/ModalDialogGenre'
import StarRating from 'vue-star-rating'
import webapi from '@/webapi'

const tracksData = {
  load: function (to) {
    return webapi.library_genre_tracks(to.params.genre)
  },

  set: function (vm, response) {
    vm.genre = vm.$route.params.genre
    vm.tracks = response.data.tracks
  }
}

export default {
  name: 'PageGenreTracks',
  mixins: [ LoadDataBeforeEnterMixin(tracksData) ],
  components: { ContentWithHeading, ListItemTrack, IndexButtonList, ModalDialogTrack, ModalDialogGenre, StarRating },

  data () {
    return {
      tracks: { items: [] },
      genre: '',

      min_rating: 0,

      show_details_modal: false,
      selected_track: {},

      show_genre_details_modal: false
    }
  },

  computed: {
    modal_obj () {
      var tracks = this.min_rating === 0 ? this.tracks.items : this.tracks.items.filter(a => a.rating >= this.min_rating)
      return {
        'name': this.genre,
        'album_count': new Set(tracks.map(track => track.album_id)).size,
        'artist_count': new Set(tracks.map(track => track.artist_id)).size,
        'track_count': tracks.length,
        'uri': tracks.map(a => a.uri).join(',')
      }
    },

    index_list () {
      return [...new Set(this.tracks.items
        .map(track => track.title_sort.charAt(0).toUpperCase()))]
    }
  },

  methods: {
    open_genre: function () {
      this.show_details_modal = false
      this.$router.push({ name: 'Genre', params: { genre: this.genre } })
    },

    play: function () {
      webapi.player_play_expression('genre is "' + this.genre + '" and media_kind is music', true)
    },

    play_track: function (position) {
      webapi.player_play_expression('genre is "' + this.genre + '" and media_kind is music', false, position)
    },

    show_rating: function (rating) {
      if (rating === 0.5) {
        rating = 0
      }
      this.min_rating = Math.ceil(rating) * 20
    },

    open_dialog: function (track) {
      this.selected_track = track
      this.show_details_modal = true
    }
  }
}
</script>

<style>
</style>
