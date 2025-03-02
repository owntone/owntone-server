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
        ? [{ key: 'dialog.track.mark-as-new', handler: this.mark_new }]
        : [{ key: 'dialog.track.mark-as-played', handler: this.mark_played }]
    },
    playable() {
      return {
        name: this.item.title,
        uri: this.item.uri,
        properties: [
          {
            key: 'property.album',
            value: this.item.album,
            handler: this.open_album
          },
          {
            key: 'property.album-artist',
            value: this.item.album_artist,
            handler: this.open_artist
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
            value: [this.item.disc_number, this.item.track_number].join(' / ')
          },
          {
            key: 'property.duration',
            value: this.$filters.toTimecode(this.item.length_ms)
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
                format: this.item.type,
                bitrate: this.item.bitrate,
                channels: this.$t('count.channels', this.item.channels),
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
        ]
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
    open_artist() {
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
    open_genre() {
      this.$emit('close')
      this.$router.push({
        name: 'genre-albums',
        params: { name: this.item.genre },
        query: { media_kind: this.item.media_kind }
      })
    }
  }
}
</script>
