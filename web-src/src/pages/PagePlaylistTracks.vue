<template>
  <div class="fd-page">
    <content-with-heading>
      <template #heading-left>
        <div class="title is-4" v-text="playlist.name" />
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <a
            class="button is-small is-light is-rounded"
            @click="show_playlist_details_modal = true"
          >
            <mdicon class="icon" name="dots-horizontal" size="16" />
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <mdicon class="icon" name="shuffle" size="16" />
            <span v-text="$t('page.playlist.shuffle')" />
          </a>
        </div>
      </template>
      <template #content>
        <p
          class="heading has-text-centered-mobile"
          v-text="$t('page.playlist.track-count', { count: tracks.count })"
        />
        <list-tracks :tracks="tracks" :uris="uris" />
        <modal-dialog-playlist
          :show="show_playlist_details_modal"
          :playlist="playlist"
          :uris="uris"
          @close="show_playlist_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupByList } from '@/lib/GroupByList'
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
    vm.tracks = new GroupByList(response[1].data)
  }
}

export default {
  name: 'PagePlaylistTracks',
  components: { ContentWithHeading, ListTracks, ModalDialogPlaylist },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },
  beforeRouteUpdate(to, from, next) {
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  },

  data() {
    return {
      playlist: {},
      tracks: new GroupByList(),
      show_playlist_details_modal: false
    }
  },

  computed: {
    uris() {
      if (this.playlist.random) {
        return this.tracks.map((a) => a.uri).join(',')
      }
      return this.playlist.uri
    }
  },

  methods: {
    play() {
      webapi.player_play_uri(this.uris, true)
    }
  }
}
</script>

<style></style>
