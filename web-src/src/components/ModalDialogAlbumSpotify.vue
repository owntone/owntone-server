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
  name: 'ModalDialogAlbumSpotify',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  computed: {
    playable() {
      return {
        name: this.item.name || '',
        image: this.item?.images?.[0]?.url || '',
        handler: this.open,
        uri: this.item.uri,
        properties: [
          {
            label: 'property.artist',
            value: this.item?.artists?.[0]?.name,
            handler: this.open_artist
          },
          {
            label: 'property.release-date',
            value: this.$filters.date(this.item.release_date)
          },
          {
            label: 'property.type',
            value: this.item.album_type
          }
        ]
      }
    }
  },
  methods: {
    open() {
      this.$emit('close')
      this.$router.push({
        name: 'music-spotify-album',
        params: { id: this.item.id }
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
