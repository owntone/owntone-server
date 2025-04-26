<template>
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
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
import webapi from '@/webapi'

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
    Promise.all([
      webapi.library_artist(to.params.id),
      webapi.library_artist_albums(to.params.id)
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
      webapi.player_play_uri(
        this.albums.items.map((item) => item.uri).join(),
        false
      )
    }
  }
}
</script>
