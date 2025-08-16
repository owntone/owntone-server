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
        playable: this.item.is_playable,
        properties: [
          {
            handler: this.openAlbum,
            key: 'property.album',
            value: this.item.album.name
          },
          {
            handler: this.openArtist,
            key: 'property.artist',
            value: this.item.artists?.map((item) => item.name).join(', ')
          },
          {
            handler: this.openArtist,
            key: 'property.author',
            value: this.item.album.authors?.map((item) => item.name).join(', ')
          },
          {
            key: 'property.release-date',
            value: this.$formatters.toDate(this.item.album.release_date)
          },
          {
            key: 'property.release-date',
            value: this.$formatters.toDate(this.item.release_date)
          },
          {
            key: 'property.position',
            value: [this.item.disc_number, this.item.track_number].join(' / ')
          },
          {
            key: 'property.duration',
            value: this.$formatters.toTimecode(this.item.duration_ms)
          },
          { key: 'property.path', value: this.item.uri }
        ],
        uri: this.item.uri
      }
    }
  },
  methods: {
    openAlbum() {
      this.$emit('close')
      this.$router.push({
        name: 'music-spotify-album',
        params: { id: this.item.album.id }
      })
    },
    openArtist() {
      this.$emit('close')
      this.$router.push({
        name: 'music-spotify-artist',
        params: { id: this.item.artists[0].id }
      })
    }
  }
}
</script>
