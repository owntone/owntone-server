<template>
  <content-with-hero>
    <template #heading>
      <pane-hero :content="heading" />
    </template>
    <template #image>
      <control-image
        :url="album.artwork_url"
        :caption="album.name"
        class="is-clickable is-medium"
        @click="openDetails"
      />
    </template>
    <template #content>
      <list-tracks :items="tracks" :show-progress="true" :uris="album.uri" />
    </template>
  </content-with-hero>
  <modal-dialog-album
    :item="album"
    :show="showDetailsModal"
    media-kind="audiobook"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ContentWithHero from '@/templates/ContentWithHero.vue'
import ControlImage from '@/components/ControlImage.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogAlbum from '@/components/ModalDialogAlbum.vue'
import PaneHero from '@/components/PaneHero.vue'
import library from '@/api/library'
import queue from '@/api/queue'

export default {
  name: 'PageAudiobooksAlbum',
  components: {
    ContentWithHero,
    ControlImage,
    ListTracks,
    ModalDialogAlbum,
    PaneHero
  },
  beforeRouteEnter(to, from, next) {
    Promise.all([
      library.album(to.params.id),
      library.albumTracks(to.params.id)
    ]).then(([album, tracks]) => {
      next((vm) => {
        vm.album = album
        vm.tracks = new GroupedList(tracks)
      })
    })
  },
  data() {
    return {
      album: {},
      showDetailsModal: false,
      tracks: new GroupedList()
    }
  },
  computed: {
    heading() {
      return {
        count: this.$t('data.tracks', { count: this.album.track_count }),
        handler: this.openArtist,
        subtitle: this.album.artist,
        title: this.album.name,
        actions: [
          { handler: this.play, icon: 'play', key: 'actions.play' },
          { handler: this.openDetails, icon: 'dots-horizontal' }
        ]
      }
    }
  },
  methods: {
    openArtist() {
      this.showDetailsModal = false
      this.$router.push({
        name: 'audiobooks-artist',
        params: { id: this.album.artist_id }
      })
    },
    openDetails() {
      this.showDetailsModal = true
    },
    play() {
      queue.playUri(this.album.uri, false)
    }
  }
}
</script>
