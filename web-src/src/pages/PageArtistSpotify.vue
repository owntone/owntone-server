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
        :button="{ handler: play, icon: 'shuffle', key: 'actions.shuffle' }"
      />
    </template>
    <template #content>
      <list-albums-spotify v-if="albums.length" :items="albums" :load="load" />
    </template>
  </content-with-heading>
  <modal-dialog-artist-spotify
    :item="artist"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import ListAlbumsSpotify from '@/components/ListAlbumsSpotify.vue'
import ModalDialogArtistSpotify from '@/components/ModalDialogArtistSpotify.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import queue from '@/api/queue'
import services from '@/api/services'

const PAGE_SIZE = 50

export default {
  name: 'PageArtistSpotify',
  components: {
    ContentWithHeading,
    ControlButton,
    ListAlbumsSpotify,
    ModalDialogArtistSpotify,
    PaneTitle
  },
  data() {
    return {
      albums: [],
      artist: {},
      offset: 0,
      showDetailsModal: false,
      total: 0
    }
  },
  computed: {
    heading() {
      return {
        subtitle: [{ count: this.total, key: 'data.albums' }],
        title: this.artist.name
      }
    }
  },
  async mounted() {
    const { api, configuration } = await services.spotify()
    const [artist, albums] = await Promise.all([
      api.artists.get(this.$route.params.id),
      api.artists.albums(
        this.$route.params.id,
        'album,single',
        configuration.webapi_country,
        PAGE_SIZE,
        0
      )
    ])
    this.artist = artist
    this.appendAlbums(albums)
  },
  methods: {
    appendAlbums(data) {
      this.albums = this.albums.concat(data.items)
      this.total = data.total
      this.offset += data.limit
    },
    async load({ loaded }) {
      const { api, configuration } = await services.spotify()
      const albums = await api.artists.albums(
        this.artist.id,
        'album,single',
        configuration.webapi_country,
        PAGE_SIZE,
        this.offset
      )
      this.appendAlbums(albums)
      loaded(albums.items.length, PAGE_SIZE)
    },
    openDetails() {
      this.showDetailsModal = true
    },
    play() {
      this.showDetailsModal = false
      queue.playUri(this.artist.uri, true)
    }
  }
}
</script>
