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
import webapi from '@/webapi'

export default {
  name: 'ModalDialogTrack',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close', 'play-count-changed'],
  computed: {
    buttons() {
      if (this.item.media_kind !== 'podcast') {
        return []
      }
      return this.item.play_count > 0
        ? [{ handler: this.markAsNew, key: 'actions.mark-as-new' }]
        : [{ handler: this.markAsPlayed, key: 'actions.mark-as-played' }]
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
            handler: this.openArtist,
            key: 'property.album-artist',
            value: this.item.album_artist
          },
          { key: 'property.composer', value: this.item.composer },
          {
            key: 'property.release-date',
            value: this.$filters.toDate(this.item.date_released)
          },
          { key: 'property.year', value: this.item.year },
          { key: 'property.genre', value: this.item.genre },
          {
            key: 'property.position',
            value:
              this.item.track_number > 0 &&
              [this.item.disc_number, this.item.track_number].join(' / ')
          },
          {
            key: 'property.duration',
            value:
              this.item.length_ms > 0 &&
              this.$filters.toTimecode(this.item.length_ms)
          },
          {
            key: 'property.type',
            value: `${this.$t(`media.kind.${this.item.media_kind}`)} - ${this.$t(`data.kind.${this.item.data_kind}`)}`
          },
          {
            key: 'property.quality',
            value:
              this.item.data_kind !== 'spotify' &&
              this.$t('dialog.track.quality-value', {
                bitrate: this.item.bitrate,
                channels: this.$t('data.channels', this.item.channels),
                format: this.item.type,
                samplerate: this.item.samplerate
              })
          },
          {
            key: 'property.added-on',
            value: this.$filters.toDateTime(this.item.time_added)
          },
          {
            key: 'property.rating',
            value: this.$t('dialog.track.rating-value', {
              rating: Math.floor(this.item.rating / 10)
            })
          },
          { key: 'property.comment', value: this.item.comment },
          { key: 'property.path', value: this.item.path }
        ],
        uri: this.item.uri
      }
    }
  },
  methods: {
    markAsNew() {
      webapi
        .library_track_update(this.item.id, { play_count: 'reset' })
        .then(() => {
          this.$emit('play-count-changed')
          this.$emit('close')
        })
    },
    markAsPlayed() {
      webapi
        .library_track_update(this.item.id, { play_count: 'increment' })
        .then(() => {
          this.$emit('play-count-changed')
          this.$emit('close')
        })
    },
    openAlbum() {
      this.$emit('close')
      if (this.item.media_kind === 'podcast') {
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
    openArtist() {
      this.$emit('close')
      if (
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
    }
  }
}
</script>
