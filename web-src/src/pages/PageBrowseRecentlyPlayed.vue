<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Recently played</p>
        <p class="heading">tracks</p>
      </template>
      <template slot="content">
        <list-item-track v-for="track in recently_played.items" :key="track.id" :track="track" @click="play_track(track)">
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
import TabsMusic from '@/components/TabsMusic'
import ListItemTrack from '@/components/ListItemTrack'
import ModalDialogTrack from '@/components/ModalDialogTrack'
import webapi from '@/webapi'

const browseData = {
  load: function (to) {
    return webapi.search({
      type: 'track',
      expression: 'time_played after 8 weeks ago and media_kind is music order by time_played desc',
      limit: 50
    })
  },

  set: function (vm, response) {
    vm.recently_played = response.data.tracks
  }
}

export default {
  name: 'PageBrowseType',
  mixins: [ LoadDataBeforeEnterMixin(browseData) ],
  components: { ContentWithHeading, TabsMusic, ListItemTrack, ModalDialogTrack },

  data () {
    return {
      recently_played: {},

      show_details_modal: false,
      selected_track: {}
    }
  },

  methods: {
    open_dialog: function (track) {
      this.selected_track = track
      this.show_details_modal = true
    },

    play_track: function (track) {
      webapi.player_play_uri(track.uri, false)
    }
  }
}
</script>

<style>
</style>
