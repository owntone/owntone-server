<template>
  <content-with-heading>
    <template #heading-left>
      <p class="title is-4" v-text="artist.name" />
    </template>
    <template #heading-right>
      <div class="buttons is-centered">
        <a class="button is-small is-light is-rounded" @click="show_artist_details_modal = true">
          <mdicon class="icon" name="dots-horizontal" size="16" />
        </a>
        <a class="button is-small is-dark is-rounded" @click="play">
          <mdicon class="icon" name="play" size="16" />
          <span v-text="$t('page.audiobooks.artist.shuffle')" />
        </a>
      </div>
    </template>
    <template #content>
      <p class="heading has-text-centered-mobile" v-text="$t('page.audiobooks.artist.album-count', { count: artist.album_count })" />
      <list-albums :albums="albums" />
      <modal-dialog-artist :show="show_artist_details_modal" :artist="artist" @close="show_artist_details_modal = false" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
import webapi from '@/webapi'
import { GroupByList } from '../lib/GroupByList'

const dataObject = {
  load: function (to) {
    return Promise.all([
      webapi.library_artist(to.params.artist_id),
      webapi.library_artist_albums(to.params.artist_id)
    ])
  },

  set: function (vm, response) {
    vm.artist = response[0].data
    vm.albums = new GroupByList(response[1].data)
  }
}

export default {
  name: 'PageAudiobooksArtist',
  components: { ContentWithHeading, ListAlbums, ModalDialogArtist },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },

  beforeRouteUpdate(to, from, next) {
    if (!this.albums.isEmpty()) {
      next()
      return
    }
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  },

  data() {
    return {
      artist: {},
      albums: new GroupByList(),

      show_artist_details_modal: false
    }
  },

  methods: {
    play: function () {
      webapi.player_play_uri(
        this.albums.items.map((a) => a.uri).join(','),
        false
      )
    }
  }
}
</script>

<style></style>
