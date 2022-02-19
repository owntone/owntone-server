<template>
  <div>
    <list-item-track v-for="(track, index) in tracks" :key="track.id" :track="track" @click="play_track(index, track)">
      <template v-slot:actions>
        <a @click.prevent.stop="open_dialog(track)">
          <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
        </a>
      </template>
    </list-item-track>
    <modal-dialog-track :show="show_details_modal" :track="selected_track" @close="show_details_modal = false" />
  </div>
</template>

<script>
import ListItemTrack from '@/components/ListItemTrack.vue'
import ModalDialogTrack from '@/components/ModalDialogTrack.vue'
import webapi from '@/webapi'

export default {
  name: 'ListTracks',
  components: { ListItemTrack, ModalDialogTrack },

  props: ['tracks', 'uris', 'expression'],

  data () {
    return {
      show_details_modal: false,
      selected_track: {}
    }
  },

  methods: {
    play_track: function (position, track) {
      if (this.uris) {
        webapi.player_play_uri(this.uris, false, position)
      } else if (this.expression) {
        webapi.player_play_expression(this.expression, false, position)
      } else {
        webapi.player_play_uri(track.uri, false)
      }
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
