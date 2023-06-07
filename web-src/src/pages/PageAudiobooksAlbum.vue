<template>
  <content-with-hero>
    <template #heading-left>
      <h1 class="title is-5" v-text="album.name" />
      <h2 class="subtitle is-6 has-text-link has-text-weight-normal">
        <a class="has-text-link" @click="open_artist" v-text="album.artist" />
      </h2>
      <div class="buttons fd-is-centered-mobile fd-has-margin-top">
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon"><mdicon name="play" size="16" /></span>
          <span v-text="$t('page.audiobooks.album.play')" />
        </a>
        <a
          class="button is-small is-light is-rounded"
          @click="show_album_details_modal = true"
        >
          <span class="icon"><mdicon name="dots-horizontal" size="16" /></span>
        </a>
      </div>
    </template>
    <template #heading-right>
      <cover-artwork
        :artwork_url="album.artwork_url"
        :artist="album.artist"
        :album="album.name"
        class="fd-has-action fd-has-shadow fd-cover fd-cover-medium-image"
        @click="show_album_details_modal = true"
      />
    </template>
    <template #content>
      <p
        class="heading is-7 has-text-centered-mobile fd-has-margin-top"
        v-text="
          $t('page.audiobooks.album.track-count', { count: album.track_count })
        "
      />
      <list-tracks :tracks="tracks" :uris="album.uri" />
      <modal-dialog-album
        :show="show_album_details_modal"
        :album="album"
        :media_kind="'audiobook'"
        @close="show_album_details_modal = false"
      />
    </template>
  </content-with-hero>
</template>

<script>
import ContentWithHero from '@/templates/ContentWithHero.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogAlbum from '@/components/ModalDialogAlbum.vue'
import CoverArtwork from '@/components/CoverArtwork.vue'
import webapi from '@/webapi'
import { GroupByList } from '@/lib/GroupByList'

const dataObject = {
  load: function (to) {
    return Promise.all([
      webapi.library_album(to.params.album_id),
      webapi.library_album_tracks(to.params.album_id)
    ])
  },

  set: function (vm, response) {
    vm.album = response[0].data
    vm.tracks = new GroupByList(response[1].data)
  }
}

export default {
  name: 'PageAudiobooksAlbum',
  components: { ContentWithHero, ListTracks, ModalDialogAlbum, CoverArtwork },

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
      album: {},
      tracks: new GroupByList(),
      show_album_details_modal: false
    }
  },

  methods: {
    open_artist: function () {
      this.show_details_modal = false
      this.$router.push({ path: '/audiobooks/artists/' + this.album.artist_id })
    },

    play: function () {
      webapi.player_play_uri(this.album.uri, false)
    }
  }
}
</script>

<style></style>
