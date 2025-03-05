<template>
  <div>
    <content-with-hero>
      <template #heading-left>
        <div class="title is-5" v-text="album.name" />
        <div class="subtitle is-6">
          <a @click="open_artist" v-text="album.artist" />
        </div>
        <div
          class="is-size-7 is-uppercase has-text-centered-mobile"
          v-text="$t('count.tracks', { count: album.track_count })"
        />
        <div class="buttons is-centered-mobile mt-5">
          <control-button
            :button="{ handler: play, icon: 'shuffle', key: 'actions.shuffle' }"
          />
          <control-button
            :button="{ handler: showDetails, icon: 'dots-horizontal' }"
          />
        </div>
      </template>
      <template #heading-right>
        <control-image
          :url="album.artwork_url"
          :artist="album.artist"
          :album="album.name"
          class="is-clickable is-medium"
          @click="showDetails"
        />
      </template>
      <template #content>
        <list-tracks :items="tracks" :uris="album.uri" />
        <modal-dialog-album
          :item="album"
          :show="show_details_modal"
          @close="show_details_modal = false"
        />
      </template>
    </content-with-hero>
  </div>
</template>

<script>
import ContentWithHero from '@/templates/ContentWithHero.vue'
import ControlButton from '@/components/ControlButton.vue'
import ControlImage from '@/components/ControlImage.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogAlbum from '@/components/ModalDialogAlbum.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_album(to.params.id),
      webapi.library_album_tracks(to.params.id)
    ])
  },

  set(vm, response) {
    vm.album = response[0].data
    vm.tracks = new GroupedList(response[1].data, {
      criteria: [{ field: 'disc_number', type: Number }],
      index: { field: 'disc_number', type: Number }
    })
    if (vm.tracks.indices.length < 2) {
      vm.tracks.group()
    }
  }
}

export default {
  name: 'PageAlbum',
  components: {
    ContentWithHero,
    ControlButton,
    ControlImage,
    ListTracks,
    ModalDialogAlbum
  },
  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },
  data() {
    return {
      album: {},
      show_details_modal: false,
      tracks: new GroupedList()
    }
  },
  methods: {
    open_artist() {
      this.show_details_modal = false
      this.$router.push({
        name: 'music-artist',
        params: { id: this.album.artist_id }
      })
    },
    play() {
      webapi.player_play_uri(this.album.uri, true)
    },
    showDetails() {
      this.show_details_modal = true
    }
  }
}
</script>
