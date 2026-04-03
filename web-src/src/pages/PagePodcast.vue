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
      <list-tracks
        :items="tracks"
        :show-progress="true"
        @play-count-changed="reloadTracks"
      />
      <modal-dialog-album
        :item="album"
        :show="showDetailsModal"
        media-kind="podcast"
        @close="showDetailsModal = false"
        @play-count-changed="reloadTracks"
        @podcast-deleted="podcastDeleted"
      />
    </template>
  </content-with-hero>
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
  name: 'PagePodcast',
  components: {
    ContentWithHero,
    ControlImage,
    ListTracks,
    ModalDialogAlbum,
    PaneHero
  },
  data() {
    return { album: {}, showDetailsModal: false, tracks: new GroupedList() }
  },
  computed: {
    heading() {
      return {
        count: this.$t('data.tracks', { count: this.album.track_count }),
        subtitle: '',
        title: this.album.name,
        actions: [
          { handler: this.play, icon: 'play', key: 'actions.play' },
          { handler: this.openDetails, icon: 'dots-horizontal' }
        ]
      }
    }
  },
  async mounted() {
    const [album, tracks] = await Promise.all([
      library.album(this.$route.params.id),
      library.podcastEpisodes(this.$route.params.id)
    ])
    this.album = album
    this.tracks = new GroupedList(tracks)
  },
  methods: {
    openDetails() {
      this.showDetailsModal = true
    },
    play() {
      queue.playUri(this.album.uri, false)
    },
    podcastDeleted() {
      this.$router.push({ name: 'podcasts' })
    },
    async reloadTracks() {
      const tracks = await library.podcastEpisodes(this.album.id)
      this.tracks = new GroupedList(tracks)
    }
  }
}
</script>
