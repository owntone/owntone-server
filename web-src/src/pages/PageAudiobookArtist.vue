<template>
  <content-with-heading>
    <template #heading>
      <pane-title :content="heading" />
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
    </template>
  </content-with-heading>
  <modal-dialog-artist
    :item="artist"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import library from '@/api/library'
import queue from '@/api/queue'

export default {
  name: 'PageAudiobookArtist',
  components: {
    ContentWithHeading,
    ControlButton,
    ListAlbums,
    ModalDialogArtist,
    PaneTitle
  },
  beforeRouteEnter(to, from, next) {
    Promise.all([
      library.artist(to.params.id),
      library.artistAlbums(to.params.id)
    ]).then(([artist, albums]) => {
      next((vm) => {
        vm.artist = artist
        vm.albums = new GroupedList(albums)
      })
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
            { count: this.artist.album_count, key: 'data.audiobooks' }
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
      queue.playUri(this.albums.items.map((item) => item.uri).join(), false)
    }
  }
}
</script>
