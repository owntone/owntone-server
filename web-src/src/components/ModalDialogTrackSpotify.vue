<template>
  <modal-dialog-playable
    :item="playable"
    :show="show"
    @close="$emit('close')"
  />
</template>

<script>
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'

export default {
  name: 'ModalDialogTrackSpotify',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  computed: {
    playable() {
      if (!this.item.artists) {
        return {}
      }
      return {
        name: this.item.name,
        uri: this.item.uri,
        properties: [
          {
            label: 'property.album',
            value: this.item.album.name,
            handler: this.open_album
          },
          {
            label: 'property.album-artist',
            value: this.item.artists[0]?.name,
            handler: this.open_artist
          },
          {
            label: 'property.release-date',
            value: this.$filters.toDate(this.item.album.release_date)
          },
          {
            label: 'property.position',
            value: [this.item.disc_number, this.item.track_number].join(' / ')
          },
          {
            label: 'property.duration',
            value: this.$filters.toTimecode(this.item.duration_ms)
          },
          { label: 'property.path', value: this.item.uri }
        ]
      }
    }
  },
  methods: {
    open_album() {
      this.$emit('close')
      this.$router.push({
        name: 'music-spotify-album',
        params: { id: this.item.album.id }
      })
    },
    open_artist() {
      this.$emit('close')
      this.$router.push({
        name: 'music-spotify-artist',
        params: { id: this.item.artists[0].id }
      })
    }
  }
}
</script>
