<template>
  <div>
    <content-with-heading>
      <template #heading-left>
        <heading-title :content="heading" />
      </template>
      <template #heading-right>
        <control-button
          :button="{ handler: showDetails, icon: 'dots-horizontal' }"
        />
        <control-button
          :button="{
            handler: play,
            icon: 'shuffle',
            key: 'page.playlist.shuffle',
            disabled: tracks.count === 0
          }"
        />
      </template>
      <template #content>
        <list-tracks :items="tracks" :uris="uris" />
        <modal-dialog-playlist
          :item="playlist"
          :show="show_details_modal"
          :uris="uris"
          @close="show_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogPlaylist from '@/components/ModalDialogPlaylist.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_playlist(to.params.id),
      webapi.library_playlist_tracks(to.params.id)
    ])
  },
  set(vm, response) {
    vm.playlist = response[0].data
    vm.tracks = new GroupedList(response[1].data)
  }
}

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
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },
  data() {
    return {
      playlist: {},
      show_details_modal: false,
      tracks: new GroupedList()
    }
  },
  computed: {
    heading() {
      return {
        title: this.playlist.name,
        subtitle: [{ key: 'count.tracks', count: this.tracks.count }]
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
    showDetails() {
      this.show_details_modal = true
    }
  }
}
</script>
