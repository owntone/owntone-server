<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    :title="item.title"
    @close="$emit('close')"
  >
    <template #content>
      <list-properties :item="playable" />
    </template>
  </modal-dialog>
</template>

<script>
import ListProperties from '@/components/ListProperties.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import player from '@/api/player'
import queue from '@/api/queue'
import { useServicesStore } from '@/stores/services'

export default {
  name: 'ModalDialogQueueItem',
  components: { ListProperties, ModalDialog },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  setup() {
    return { servicesStore: useServicesStore() }
  },
  data() {
    return {
      spotifyTrack: {}
    }
  },
  computed: {
    actions() {
      return [
        { handler: this.remove, icon: 'delete', key: 'actions.remove' },
        { handler: this.play, icon: 'play', key: 'actions.play' }
      ]
    },
    playable() {
      return {
        name: this.item.title,
        properties: [
          {
            handler: this.openAlbum,
            key: 'property.album',
            value: this.item.album
          },
          {
            handler: this.openAlbumArtist,
            key: 'property.album-artist',
            value: this.item.album_artist
          },
          { key: 'property.composer', value: this.item.composer },
          { key: 'property.year', value: this.item.year },
          {
            handler: this.openGenre,
            key: 'property.genre',
            value: this.item.genre
          },
          {
            key: 'property.position',
            value: [this.item.disc_number, this.item.track_number].join(' / ')
          },
          {
            key: 'property.duration',
            value: this.$filters.toTimecode(this.item.length_ms)
          },
          { key: 'property.path', value: this.item.path },
          {
            key: 'property.type',
            value: `${this.$t(`media.kind.${this.item.media_kind}`)} - ${this.$t(`data.kind.${this.item.data_kind}`)}`
          },
          {
            key: 'property.quality',
            value: this.$t('dialog.track.quality-value', {
              bitrate: this.item.bitrate,
              channels: this.$t('data.channels', this.item.channels),
              format: this.item.type,
              samplerate: this.item.samplerate
            })
          }
        ],
        uri: this.item.uri
      }
    }
  },
  watch: {
    item() {
      if (this.item?.data_kind === 'spotify') {
        const spotifyApi = new SpotifyWebApi()
        spotifyApi.setAccessToken(this.servicesStore.spotify.webapi_token)
        spotifyApi
          .getTrack(this.item.path.slice(this.item.path.lastIndexOf(':') + 1))
          .then((response) => {
            this.spotifyTrack = response
          })
      } else {
        this.spotifyTrack = {}
      }
    }
  },
  methods: {
    openAlbum() {
      this.$emit('close')
      if (this.item.data_kind === 'spotify') {
        this.$router.push({
          name: 'music-spotify-album',
          params: { id: this.spotifyTrack.album.id }
        })
      } else if (this.item.media_kind === 'podcast') {
        this.$router.push({
          name: 'podcast',
          params: { id: this.item.album_id }
        })
      } else if (this.item.media_kind === 'audiobook') {
        this.$router.push({
          name: 'audiobooks-album',
          params: { id: this.item.album_id }
        })
      } else if (this.item.media_kind === 'music') {
        this.$router.push({
          name: 'music-album',
          params: { id: this.item.album_id }
        })
      }
    },
    openAlbumArtist() {
      this.$emit('close')
      if (this.item.data_kind === 'spotify') {
        this.$router.push({
          name: 'music-spotify-artist',
          params: { id: this.spotifyTrack.artists[0].id }
        })
      } else if (
        this.item.media_kind === 'music' ||
        this.item.media_kind === 'podcast'
      ) {
        this.$router.push({
          name: 'music-artist',
          params: { id: this.item.album_artist_id }
        })
      } else if (this.item.media_kind === 'audiobook') {
        this.$router.push({
          name: 'audiobooks-artist',
          params: { id: this.item.album_artist_id }
        })
      }
    },
    openGenre() {
      this.$emit('close')
      this.$router.push({
        name: 'genre-albums',
        params: { name: this.item.genre },
        query: { mediaKind: this.item.media_kind }
      })
    },
    play() {
      this.$emit('close')
      player.play({ item_id: this.item.id })
    },
    remove() {
      this.$emit('close')
      queue.remove(this.item.id)
    }
  }
}
</script>
