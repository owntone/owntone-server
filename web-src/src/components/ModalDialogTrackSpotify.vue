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
      return {
        name: this.item.name,
        subtitle: this.item.artists[0].name,
        properties: [
          {
            label: 'dialog.spotify.track.album',
            value: this.item.album.name,
            action: this.open_album
          },
          {
            label: 'dialog.spotify.track.album-artist',
            value: this.item.artists[0].name,
            action: this.open_artist
          },
          {
            label: 'dialog.spotify.track.release-date',
            value: this.$filters.date(item.album.release_date)
          },
          {
            label: 'dialog.spotify.track.position',
            value: [item.disc_number, item.track_number].join(' / ')
          },
          {
            label: 'dialog.spotify.track.duration',
            value: this.$filters.durationInHours(item.duration_ms)
          },
          { label: 'dialog.spotify.track.path', value: this.item.uri }
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
