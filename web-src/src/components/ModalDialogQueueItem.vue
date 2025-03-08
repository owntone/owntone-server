<template>
  <modal-dialog :actions="actions" :show="show" @close="$emit('close')">
    <template #content>
      <list-properties :item="playable" />
    </template>
  </modal-dialog>
</template>

<script>
import ListProperties from '@/components/ListProperties.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import { useServicesStore } from '@/stores/services'
import webapi from '@/webapi'

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
      spotify_track: {}
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
            handler: this.open_album,
            key: 'property.album',
            value: this.item.album
          },
          {
            handler: this.open_album_artist,
            key: 'property.album-artist',
            value: this.item.album_artist
          },
          { key: 'property.composer', value: this.item.composer },
          { key: 'property.year', value: this.item.year },
          {
            handler: this.open_genre,
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
              channels: this.$t('count.channels', this.item.channels),
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
            this.spotify_track = response
          })
      } else {
        this.spotify_track = {}
      }
    }
  },
  methods: {
    open_album() {
      this.$emit('close')
      if (this.item.data_kind === 'spotify') {
        this.$router.push({
          name: 'music-spotify-album',
          params: { id: this.spotify_track.album.id }
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
    open_album_artist() {
      this.$emit('close')
      if (this.item.data_kind === 'spotify') {
        this.$router.push({
          name: 'music-spotify-artist',
          params: { id: this.spotify_track.artists[0].id }
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
    open_genre() {
      this.$emit('close')
      this.$router.push({
        name: 'genre-albums',
        params: { name: this.item.genre },
        query: { media_kind: this.item.media_kind }
      })
    },
    play() {
      this.$emit('close')
      webapi.player_play({ item_id: this.item.id })
    },
    remove() {
      this.$emit('close')
      webapi.queue_remove(this.item.id)
    }
  }
}
</script>
