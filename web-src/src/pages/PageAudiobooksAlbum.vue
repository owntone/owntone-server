<template>
  <div class="fd-page">
    <content-with-hero>
      <template #heading-left>
        <h1 class="title is-5" v-text="album.name" />
        <h2 class="subtitle is-6 has-text-link has-text-weight-normal">
          <a class="has-text-link" @click="open_artist" v-text="album.artist" />
        </h2>
        <div class="buttons fd-is-centered-mobile mt-5">
          <a class="button is-small is-dark is-rounded" @click="play">
            <mdicon class="icon" name="play" size="16" />
            <span v-text="$t('page.audiobooks.album.play')" />
          </a>
          <a
            class="button is-small is-light is-rounded"
            @click="show_album_details_modal = true"
          >
            <mdicon class="icon" name="dots-horizontal" size="16" />
          </a>
        </div>
      </template>
      <template #heading-right>
        <cover-artwork
          :artwork_url="album.artwork_url"
          :artist="album.artist"
          :album="album.name"
          class="is-clickable fd-has-shadow fd-cover fd-cover-medium-image"
          @click="show_album_details_modal = true"
        />
      </template>
      <template #content>
        <p
          class="heading is-7 has-text-centered-mobile mt-5"
          v-text="
            $t('page.audiobooks.album.track-count', {
              count: album.track_count
            })
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
  </div>
</template>

<script>
import ContentWithHero from '@/templates/ContentWithHero.vue'
import CoverArtwork from '@/components/CoverArtwork.vue'
import { GroupByList } from '@/lib/GroupByList'
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
    vm.tracks = new GroupByList(response[1].data)
  }
}

export default {
  name: 'PageAudiobooksAlbum',
  components: { ContentWithHero, CoverArtwork, ListTracks, ModalDialogAlbum },

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
    open_artist() {
      this.show_details_modal = false
      this.$router.push({
        name: 'audiobooks-artist',
        params: { id: this.album.artist_id }
      })
    },

    play() {
      webapi.player_play_uri(this.album.uri, false)
    }
  }
}
</script>

<style></style>
