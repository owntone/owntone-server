<template>
  <content-with-heading>
    <template slot="heading-left">
      <div class="title is-4">{{ album.name }}</div>
      <div class="title is-4 has-text-grey has-text-weight-normal">{{ album.artist }}</div>
    </template>
    <template slot="heading-right">
      <a class="button is-small is-dark is-rounded" @click="play">
        <span class="icon">
          <i class="mdi mdi-play"></i>
        </span>
        <span>Play</span>
      </a>
    </template>
    <template slot="content">
      <p class="heading has-text-centered-mobile">{{ album.track_count }} tracks</p>
      <list-item-track v-for="(track, index) in tracks" :key="track.id" :track="track" :position="index" :context_uri="album.uri"></list-item-track>
    </template>
  </content-with-heading>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import ListItemTrack from '@/components/ListItemTrack'
import webapi from '@/webapi'

const albumData = {
  load: function (to) {
    return Promise.all([
      webapi.library_album(to.params.album_id),
      webapi.library_album_tracks(to.params.album_id)
    ])
  },

  set: function (vm, response) {
    vm.album = response[0].data
    vm.tracks = response[1].data.items
  }
}

export default {
  name: 'PageAudiobook',
  mixins: [ LoadDataBeforeEnterMixin(albumData) ],
  components: { ContentWithHeading, ListItemTrack },

  data () {
    return {
      album: {},
      tracks: []
    }
  },

  methods: {
    play: function () {
      webapi.queue_clear().then(() =>
        webapi.queue_add(this.album.uri).then(() =>
          webapi.player_play()
        )
      )
    }
  }
}
</script>

<style>
</style>
