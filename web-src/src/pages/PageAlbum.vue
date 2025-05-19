<template>
  <content-with-hero>
    <template #heading>
      <pane-hero :content="heading" />
    </template>
    <template #image>
      <control-image
        :url="album.artwork_url"
        :caption="album.name"
        class="is-medium"
        @click="openDetails"
      />
    </template>
    <template #content>
      <list-tracks :items="tracks" :uris="album.uri" />
    </template>
  </content-with-hero>
  <modal-dialog-album
    :item="album"
    :show="showDetailsModal"
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
  name: 'PageAlbum',
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
        vm.tracks = new GroupedList(tracks, {
          criteria: [{ field: 'disc_number', type: Number }],
          index: { field: 'disc_number', type: Number }
        })
        if (vm.tracks.indices.length < 2) {
          vm.tracks.group()
        }
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
          { handler: this.play, icon: 'shuffle', key: 'actions.shuffle' },
          { handler: this.openDetails, icon: 'dots-horizontal' }
        ]
      }
    }
  },
  methods: {
    openArtist() {
      this.showDetailsModal = false
      this.$router.push({
        name: 'music-artist',
        params: { id: this.album.artist_id }
      })
    },
    openDetails() {
      this.showDetailsModal = true
    },
    play() {
      queue.playUri(this.album.uri, true)
    }
  }
}
</script>
