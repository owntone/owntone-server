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
            { label: 'dialog.album.mark-as-played', handler: this.mark_played },
            {
              label: 'dialog.album.remove-podcast',
              handler: this.remove_podcast
            }
          ]
        }
        return [
          { label: 'dialog.album.mark-as-played', handler: this.mark_played }
        ]
      }
      return []
    },
    media_kind_resolved() {
      return this.media_kind || this.item.media_kind
    },
    playable() {
      return {
        name: this.item.name,
        handler: this.open,
        image: this.item.artwork_url,
        uri: this.item.uri,
        properties: [
          {
            label: 'property.artist',
            value: this.item.artist,
            handler: this.open_artist
          },
          {
            label: 'property.release-date',
            value: this.$filters.date(this.item.date_released)
          },
          { label: 'property.year', value: this.item.year },
          { label: 'property.tracks', value: this.item.track_count },
          {
            label: 'property.duration',
            value: this.$filters.durationInHours(this.item.length_ms)
          },
          {
            label: 'property.type',
            value: `${this.$t(`media.kind.${this.item.media_kind}`)} - ${this.$t(`data.kind.${this.item.data_kind}`)}`
          },
          {
            label: 'property.added-on',
            value: this.$filters.datetime(this.item.time_added)
          }
        ]
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
