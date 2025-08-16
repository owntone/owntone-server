<template>
  <content-with-hero>
    <template #heading>
      <pane-hero :content="heading" />
    </template>
    <template #image>
      <control-image
        :url="album.images?.[0]?.url ?? ''"
        :caption="album.name"
        class="is-medium"
        @click="openDetails"
      />
    </template>
    <template #content>
      <list-chapters-spotify :items="tracks" :context-uri="album.uri" />
    </template>
  </content-with-hero>
  <modal-dialog-album-spotify
    :item="album"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ContentWithHero from '@/templates/ContentWithHero.vue'
import ControlImage from '@/components/ControlImage.vue'
import ListChaptersSpotify from '@/components/ListChaptersSpotify.vue'
import ModalDialogAlbumSpotify from '@/components/ModalDialogAlbumSpotify.vue'
import PaneHero from '@/components/PaneHero.vue'
import queue from '@/api/queue'
import services from '@/api/services'

export default {
  name: 'PageAudiobookSpotify',
  components: {
    ContentWithHero,
    ControlImage,
    ListChaptersSpotify,
    ModalDialogAlbumSpotify,
    PaneHero
  },
  beforeRouteEnter(to, from, next) {
    services.spotify().then(({ api, configuration }) => {
      api.audiobooks
        .get(to.params.id, configuration.webapi_country)
        .then((album) => {
          next((vm) => {
            vm.album = album
          })
        })
    })
  },
  data() {
    return {
      album: { authors: [{}], chapters: {} },
      showDetailsModal: false
    }
  },
  computed: {
    heading() {
      return {
        actions: [
          { handler: this.play, icon: 'shuffle', key: 'actions.shuffle' },
          { handler: this.openDetails, icon: 'dots-horizontal' }
        ],
        count: this.$t('data.tracks', { count: this.album.chapters.total }),
        subtitle: this.album.authors.map((item) => item.name).join(', '),
        title: this.album.name
      }
    },
    tracks() {
      const { album } = this
      if (album.chapters.total) {
        return album.chapters.items.map((track) => ({ ...track, album }))
      }
      return []
    }
  },
  methods: {
    openDetails() {
      this.showDetailsModal = true
    },
    play() {
      queue.playUri(this.album.uri, true)
    }
  }
}
</script>
