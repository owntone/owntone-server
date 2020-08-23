<template>
  <div>
    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Radio</p>
      </template>
      <template slot="content">
        <p class="heading has-text-centered-mobile">{{ tracks.total }} tracks</p>
        <list-item-track v-for="track in tracks.items" :key="track.id" :track="track" @click="play_track(track)">
          <template slot="actions">
            <a @click="open_dialog(track)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-track>
        <modal-dialog-track :show="show_details_modal" :track="selected_track" @close="show_details_modal = false" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import ListItemTrack from '@/components/ListItemTrack'
import ModalDialogTrack from '@/components/ModalDialogTrack'
import webapi from '@/webapi'

const streamsData = {
  load: function (to) {
    return webapi.library_radio_streams()
  },

  set: function (vm, response) {
    vm.tracks = response.data.tracks
  }
}

export default {
  name: 'PageRadioStreams',
  mixins: [LoadDataBeforeEnterMixin(streamsData)],
  components: { ContentWithHeading, ListItemTrack, ModalDialogTrack },

  data () {
    return {
      tracks: { items: [] },

      show_details_modal: false,
      selected_track: {}
    }
  },

  methods: {
    play_track: function (track) {
      webapi.player_play_uri(track.uri, false)
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
