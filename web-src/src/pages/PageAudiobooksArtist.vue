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
            icon: 'play',
            key: 'page.audiobooks.artist.play'
          }"
        />
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
import HeadingTitle from '@/components/HeadingTitle.vue'
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
    HeadingTitle,
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
  computed: {
    heading() {
      if (this.artist.name) {
        return {
          title: this.artist.name,
          subtitle: [
            { key: 'count.audiobooks', count: this.artist.album_count }
          ]
        }
      }
      return {}
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
