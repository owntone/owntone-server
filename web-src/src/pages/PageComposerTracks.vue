<template>
  <div>
    <content-with-heading>
      <template slot="options">
        <index-button-list :index="index_list"></index-button-list>
      </template>
      <template slot="heading-left">
        <p class="title is-4">{{ composer }}</p>
        <p class="heading">{{ tracks.total }} tracks</p>
      </template>
      <template slot="heading-right">
        <div class="buttons is-centered">
          <a class="button is-small is-light is-rounded" @click="show_composer_details_modal = true">
            <span class="icon"><i class="mdi mdi-dots-horizontal mdi-18px"></i></span>
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
          </a>
        </div>
      </template>
      <template slot="content">
        <p class="heading has-text-centered-mobile"><a class="has-text-link" @click="open_albums">albums</a> | {{ tracks.total }} tracks</p>
        <list-item-track v-for="(track, index) in tracks.items" :key="track.id" :track="track" @click="play_track(index)">
          <template slot="actions">
            <a @click="open_dialog(track)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-track>
        <modal-dialog-track :show="show_details_modal" :track="selected_track" @close="show_details_modal = false" />
        <modal-dialog-composer :show="show_composer_details_modal" :composer="{ 'name': composer }" @close="show_composer_details_modal = false" />
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
import ModalDialogComposer from '@/components/ModalDialogComposer'
import webapi from '@/webapi'

const tracksData = {
  load: function (to) {
    return webapi.library_composer(to.params.composer, 'tracks')
  },

  set: function (vm, response) {
    vm.composer = vm.$route.params.composer
    vm.tracks = response.data.tracks
  }
}

export default {
  name: 'PageComposerTracks',
  mixins: [ LoadDataBeforeEnterMixin(tracksData) ],
  components: { ContentWithHeading, ListItemTrack, IndexButtonList, ModalDialogTrack, ModalDialogComposer },

  data () {
    return {
      tracks: { items: [] },
      composer: '',

      show_details_modal: false,
      selected_track: {},

      show_composer_details_modal: false
    }
  },

  computed: {
    index_list () {
      return [...new Set(this.tracks.items
        .map(track => track.title_sort.charAt(0).toUpperCase()))]
    }
  },

  methods: {
    open_albums: function () {
      this.show_details_modal = false
      this.$router.push({ name: 'ComposerAlbums', params: { composer: this.composer } })
    },

    play: function () {
      webapi.player_play_expression('composer is "' + this.composer + '" and media_kind is music', true)
    },

    play_track: function (position) {
      webapi.player_play_expression('composer is "' + this.composer + '" and media_kind is music', false, position)
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
