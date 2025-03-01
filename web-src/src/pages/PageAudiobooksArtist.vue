<template>
  <div>
    <content-with-heading>
      <template #heading-left>
        <div class="title is-4" v-text="artist.name" />
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('count.audiobooks', { count: artist.album_count })"
        />
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <control-button :handler="showDetails" icon="dots-horizontal" />
          <control-button
            :handler="play"
            icon="play"
            label="page.audiobooks.artist.play"
          />
        </div>
      </template>
      <template #content>
        <list-albums :items="albums" />
        <modal-dialog-artist
          :item="artist"
          :show="show_details_modal"
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
import ListAlbums from '@/components/ListAlbums.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_artist(to.params.id),
      webapi.library_artist_albums(to.params.id)
    ])
  },
  set(vm, response) {
    vm.artist = response[0].data
    vm.albums = new GroupedList(response[1].data)
  }
}

export default {
  name: 'PageAudiobooksArtist',
  components: {
    ContentWithHeading,
    ControlButton,
    ListAlbums,
    ModalDialogArtist
  },
  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },
  data() {
    return {
      albums: new GroupedList(),
      artist: {},
      show_details_modal: false
    }
  },
  methods: {
    play() {
      webapi.player_play_uri(
        this.albums.items.map((item) => item.uri).join(),
        false
      )
    },
    showDetails() {
      this.show_details_modal = true
    }
  }
}
</script>
