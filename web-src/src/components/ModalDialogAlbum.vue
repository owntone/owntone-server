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
  name: 'ModalDialogAlbum',
  components: { ModalDialogPlayable },
  props: {
    item: { required: true, type: Object },
    media_kind: { default: '', type: String },
    show: Boolean
  },
  emits: ['close', 'remove-podcast', 'play-count-changed'],
  computed: {
    buttons() {
      if (this.media_kind_resolved === 'podcast') {
        if (this.item.data_kind === 'url') {
          return [
            { handler: this.mark_played, key: 'actions.mark-as-played' },
            { handler: this.remove_podcast, key: 'actions.remove-podcast' }
          ]
        }
        return [{ handler: this.mark_played, key: 'actions.mark-as-played' }]
      }
      return []
    },
    media_kind_resolved() {
      return this.media_kind || this.item.media_kind
    },
    playable() {
      return {
        handler: this.open,
        image: this.item.artwork_url,
        name: this.item.name,
        properties: [
          {
            handler: this.open_artist,
            key: 'property.artist',
            value: this.item.artist
          },
          {
            key: 'property.release-date',
            value: this.$filters.toDate(this.item.date_released)
          },
          { key: 'property.year', value: this.item.year },
          { key: 'property.tracks', value: this.item.track_count },
          {
            key: 'property.duration',
            value: this.$filters.toTimecode(this.item.length_ms)
          },
          {
            key: 'property.type',
            value: `${this.$t(`media.kind.${this.item.media_kind}`)} - ${this.$t(`data.kind.${this.item.data_kind}`)}`
          },
          {
            key: 'property.added-on',
            value: this.$filters.toDateTime(this.item.time_added)
          }
        ],
        uri: this.item.uri
      }
    }
  },
  methods: {
    mark_played() {
      webapi
        .library_album_track_update(this.item.id, { play_count: 'played' })
        .then(() => {
          this.$emit('play-count-changed')
          this.$emit('close')
        })
    },
    open() {
      this.$emit('close')
      if (this.media_kind_resolved === 'podcast') {
        this.$router.push({ name: 'podcast', params: { id: this.item.id } })
      } else if (this.media_kind_resolved === 'audiobook') {
        this.$router.push({
          name: 'audiobooks-album',
          params: { id: this.item.id }
        })
      } else {
        this.$router.push({
          name: 'music-album',
          params: { id: this.item.id }
        })
      }
    },
    open_artist() {
      this.$emit('close')
      if (this.media_kind_resolved === 'audiobook') {
        this.$router.push({
          name: 'audiobooks-artist',
          params: { id: this.item.artist_id }
        })
      } else {
        this.$router.push({
          name: 'music-artist',
          params: { id: this.item.artist_id }
        })
      }
    },
    remove_podcast() {
      this.$emit('remove-podcast')
    }
  }
}
</script>
