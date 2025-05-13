<template>
  <content-with-heading>
    <template #heading>
      <pane-title :content="heading" />
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
import PaneTitle from '@/components/PaneTitle.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogPlaylist from '@/components/ModalDialogPlaylist.vue'
import library from '@/api/library'
import queue from '@/api/queue'

export default {
  name: 'PagePlaylistTracks',
  components: {
    ContentWithHeading,
    ControlButton,
    PaneTitle,
    ListTracks,
    ModalDialogPlaylist
  },
  beforeRouteEnter(to, from, next) {
    Promise.all([
      library.playlist(to.params.id),
      library.playlistTracks(to.params.id)
    ]).then(([playlist, tracks]) => {
      next((vm) => {
        vm.playlist = playlist
        vm.tracks = new GroupedList(tracks)
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
        subtitle: [{ count: this.tracks.count, key: 'data.tracks' }],
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
      queue.playUri(this.uris, true)
    },
    openDetails() {
      this.showDetailsModal = true
    }
  }
}
</script>
