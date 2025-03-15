<template>
  <div>
    <content-with-heading>
      <template #heading>
        <heading-title :content="heading" />
      </template>
      <template #actions>
        <control-button
          :button="{ handler: openDetails, icon: 'dots-horizontal' }"
        />
        <control-button
          :button="{ handler: play, icon: 'play', key: 'actions.play' }"
        />
      </template>
      <template #content>
        <list-albums :items="albums" />
        <modal-dialog-artist
          :item="artist"
          :show="showDetailsModal"
          @close="showDetailsModal = false"
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
      showDetailsModal: false
    }
  },
  computed: {
    heading() {
      if (this.artist.name) {
        return {
          subtitle: [
            { count: this.artist.album_count, key: 'count.audiobooks' }
          ],
          title: this.artist.name
        }
      }
      return {}
    }
  },
  methods: {
    openDetails() {
      this.showDetailsModal = true
    },
    play() {
      webapi.player_play_uri(
        this.albums.items.map((item) => item.uri).join(),
        false
      )
    }
  }
}
</script>
