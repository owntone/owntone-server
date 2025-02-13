<template>
  <modal-dialog-playable
    :buttons="buttons"
    :item="playable"
    :show="show"
    @close="$emit('close')"
  />
</template>

<script>
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import { useServicesStore } from '@/stores/services'
import webapi from '@/webapi'

export default {
  name: 'ModalDialogTrack',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close', 'play-count-changed'],
  setup() {
    return { servicesStore: useServicesStore() }
  },
  data() {
    return {
      spotify_track: {}
    }
  },
  computed: {
    buttons() {
      if (this.item.media_kind === 'podcast') {
        if (this.item.play_count > 0) {
          return [{ label: 'dialog.track.mark-as-new', handler: this.mark_new }]
        }
        if (this.item.play_count === 0) {
          return [
            { label: 'dialog.track.mark-as-played', handler: this.mark_played }
          ]
        }
      }
      return []
    },
    playable() {
      return {
        name: this.item.title,
        uri: this.item.uri,
        properties: [
          {
            label: 'property.album',
            value: this.item.album,
            handler: this.open_album
          },
          {
            label: 'property.album-artist',
            value: this.item.album_artist,
            handler: this.open_artist
          },
          { label: 'property.composer', value: this.item.composer },
          {
            label: 'property.release-date',
            value: this.$filters.date(this.item.date_released)
          },
          { label: 'property.year', value: this.item.year },
          { label: 'property.genre', value: this.item.genre },
          {
            label: 'property.position',
            value: [this.item.disc_number, this.item.track_number].join(' / ')
          },
          {
            label: 'property.duration',
            value: this.$filters.durationInHours(this.item.length_ms)
          },
          {
            label: 'property.type',
            value: `${this.$t(`media.kind.${this.item.media_kind}`)} - ${this.$t(`data.kind.${this.item.data_kind}`)}`
          },
          {
            label: 'property.quality',
            value: this.$t('dialog.track.quality-value', {
              format: this.item.type,
              bitrate: this.item.bitrate,
              channels: this.$filters.channels(this.item.channels),
              samplerate: this.item.samplerate
            })
          },
          {
            label: 'property.added-on',
            value: this.$filters.datetime(this.item.time_added)
          },
          {
            label: 'property.rating',
            value: this.$t('dialog.track.rating-value', {
              rating: Math.floor(this.item.rating / 10)
            })
          },
          { label: 'property.comment', value: this.item.comment },
          { label: 'property.path', value: this.item.path }
        ]
      }
    }
  },
  watch: {
    item() {
      if (
        this.item &&
        this.item.data_kind === 'spotify' &&
        this.item.media_kind !== 'podcast'
      ) {
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
    mark_new() {
      webapi
        .library_track_update(this.item.id, { play_count: 'reset' })
        .then(() => {
          this.$emit('play-count-changed')
          this.$emit('close')
        })
    },
    mark_played() {
      webapi
        .library_track_update(this.item.id, { play_count: 'increment' })
        .then(() => {
          this.$emit('play-count-changed')
          this.$emit('close')
        })
    },
    open_album() {
      if (
        this.item.data_kind === 'spotify' &&
        this.item.media_kind !== 'podcast'
      ) {
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
    open_artist() {
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
      this.$router.push({
        name: 'genre-albums',
        params: { name: this.item.genre },
        query: { media_kind: this.item.media_kind }
      })
    }
  }
}
</script>
