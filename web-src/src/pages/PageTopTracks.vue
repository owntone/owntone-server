<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">{{ id }}</p>
        <p class="heading">Top Tracks (of {{ tracks.total }} tracks)</p>
      </template>
      <template slot="heading-right">
        <div class="buttons is-centered">
          <a class="button is-small is-light is-rounded" @click="show_top_tracks_details_modal = true">
            <span class="icon"><i class="mdi mdi-dots-horizontal mdi-18px"></i></span>
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
          </a>
        </div>
      </template>
      <template slot="content">
        <list-item-track v-for="(track, index) in tracks.items" :key="track.id" :track="track" @click="play_track(index)">
          <template slot="actions">
            <a @click="open_dialog(track)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-track>
        <modal-dialog-track :show="show_details_modal" :track="selected_track" @close="show_details_modal = false" />
        <modal-dialog-top-tracks :show="show_top_tracks_details_modal" :tracks="tracks" @close="show_top_tracks_details_modal = false" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import ListItemTrack from '@/components/ListItemTrack'
import ModalDialogTrack from '@/components/ModalDialogTrack'
import ModalDialogTopTracks from '@/components/ModalDialogTopTracks'
import webapi from '@/webapi'

const tracksData = {
  load: function (to) {
    return webapi.search({ type: 'track', expression: (to.params.condition ? to.params.condition : 'media_kind is music') + ' order by play_count desc', limit: 10 })
  },

  set: function (vm, response) {
    vm.id = vm.$route.params.id
    vm.tracks = response.data.tracks
  }
}

export default {
  name: 'PageTopTracks',
  mixins: [ LoadDataBeforeEnterMixin(tracksData) ],
  components: { ContentWithHeading, TabsMusic, ListItemTrack, ModalDialogTrack, ModalDialogTopTracks },

  data () {
    return {
      tracks: { items: [] },
      id: '',

      show_top_tracks_details_modal: false,
      show_details_modal: false,
      selected_track: {}
    }
  },

  methods: {
    play: function () {
      webapi.player_play_uri(this.tracks.items.map(a => a.uri).join(','), true)
    },

    play_track: function (position) {
      webapi.player_play_uri(this.tracks.items.map(a => a.uri).join(','), false, position)
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
