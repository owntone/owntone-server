<template>
  <content-with-heading>
    <template #heading>
      <heading-title :content="heading" />
    </template>
    <template #actions>
      <control-button
        :button="{ handler: openDetails, icon: 'dots-horizontal' }"
      />
      <control-button
        :button="{
          handler: play,
          icon: 'shuffle',
          key: 'actions.shuffle'
        }"
        :disabled="tracks.count === 0"
      />
    </template>
    <template #content>
      <list-tracks :items="tracks" :uris="uris" />
    </template>
  </content-with-heading>
  <modal-dialog-playlist
    :item="playlist"
    :show="showDetailsModal"
    :uris="uris"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogPlaylist from '@/components/ModalDialogPlaylist.vue'
import webapi from '@/webapi'

export default {
  name: 'PagePlaylistTracks',
  components: {
    ContentWithHeading,
    ControlButton,
    HeadingTitle,
    ListTracks,
    ModalDialogPlaylist
  },
  beforeRouteEnter(to, from, next) {
    Promise.all([
      webapi.library_playlist(to.params.id),
      webapi.library_playlist_tracks(to.params.id)
    ]).then(([playlist, tracks]) => {
      next((vm) => {
        vm.playlist = playlist.data
        vm.tracks = new GroupedList(tracks.data)
      })
    })
  },
  data() {
    return {
      playlist: {},
      showDetailsModal: false,
      tracks: new GroupedList()
    }
  },
  computed: {
    heading() {
      return {
        subtitle: [{ count: this.tracks.count, key: 'count.tracks' }],
        title: this.playlist.name
      }
    },
    uris() {
      if (this.playlist.random) {
        return this.tracks.map((item) => item.uri).join()
      }
      return this.playlist.uri
    }
  },
  methods: {
    play() {
      webapi.player_play_uri(this.uris, true)
    },
    openDetails() {
      this.showDetailsModal = true
    }
  }
}
</script>
