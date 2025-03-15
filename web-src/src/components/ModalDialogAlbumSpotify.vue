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
        image: this.item?.images?.[0]?.url || '',
        name: this.item.name || '',
        properties: [
          {
            handler: this.openArtist,
            key: 'property.artist',
            value: this.item?.artists?.[0]?.name
          },
          {
            key: 'property.release-date',
            value: this.$filters.toDate(this.item.release_date)
          },
          { key: 'property.type', value: this.item.album_type }
        ],
        uri: this.item.uri
      }
    }
  },
  methods: {
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
