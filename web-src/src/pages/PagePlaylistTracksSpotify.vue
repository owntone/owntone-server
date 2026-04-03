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
        :button="{
          handler: play,
          icon: 'shuffle',
          key: 'actions.shuffle'
        }"
        :disabled="playlist.tracks.total === 0"
      />
    </template>
    <template #content>
      <list-tracks-spotify
        v-if="tracks.length"
        :context-uri="playlist.uri"
        :items="tracks"
        :load="load"
      />
    </template>
  </content-with-heading>
  <modal-dialog-playlist-spotify
    :item="playlist"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import ListTracksSpotify from '@/components/ListTracksSpotify.vue'
import ModalDialogPlaylistSpotify from '@/components/ModalDialogPlaylistSpotify.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import queue from '@/api/queue'
import services from '@/api/services'

const PAGE_SIZE = 50

export default {
  name: 'PagePlaylistTracksSpotify',
  components: {
    ContentWithHeading,
    ControlButton,
    ListTracksSpotify,
    ModalDialogPlaylistSpotify,
    PaneTitle
  },
  data() {
    return {
      offset: 0,
      playlist: { tracks: {} },
      showDetailsModal: false,
      total: 0,
      tracks: []
    }
  },
  computed: {
    heading() {
      if (this.playlist.name) {
        return {
          subtitle: [{ count: this.playlist.tracks.total, key: 'data.tracks' }],
          title: this.playlist.name
        }
      }
      return {}
    }
  },
  async mounted() {
    const { api, configuration } = await services.spotify.get()
    const [playlist, tracks] = await Promise.all([
      api.playlists.getPlaylist(this.$route.params.id),
      api.playlists.getPlaylistItems(
        this.$route.params.id,
        configuration.webapi_country,
        null,
        PAGE_SIZE,
        0
      )
    ])
    this.playlist = playlist
    this.appendTracks(tracks)
  },
  methods: {
    appendTracks(data) {
      let position = Math.max(
        -1,
        ...this.tracks.map((item) => item.position).filter((item) => item)
      )
      data.items.forEach((item) => {
        const { track } = item
        if (track) {
          if (track.is_playable) {
            position += 1
            track.position = position
          }
          this.tracks.push(track)
        }
      })
      this.total = data.total
      this.offset += data.limit
    },
    async load({ loaded }) {
      const { api, configuration } = await services.spotify.get()
      const data = await api.playlists.getPlaylistItems(
        this.playlist.id,
        configuration.webapi_country,
        null,
        PAGE_SIZE,
        this.offset
      )
      this.appendTracks(data)
      loaded(data.items.length, PAGE_SIZE)
    },
    play() {
      this.showDetailsModal = false
      queue.playUri(this.playlist.uri, true)
    },
    openDetails() {
      this.showDetailsModal = true
    }
  }
}
</script>
