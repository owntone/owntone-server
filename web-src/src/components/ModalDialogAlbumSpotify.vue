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
        image: this.item.images?.[0]?.url || '',
        name: this.item.name,
        properties: [
          {
            handler: this.openArtist,
            key: 'property.artist',
            value: this.item.artists?.map((item) => item.name).join(', ')
          },
          {
            key: 'property.author',
            value: this.item.authors?.map((item) => item.name).join(', ')
          },
          { key: 'property.chapters', value: this.item.total_chapters },
          { key: 'property.edition', value: this.item.edition },
          {
            key: 'property.narrator',
            value: this.item.narrators?.map((item) => item.name).join(', ')
          },
          {
            key: 'property.release-date',
            value: this.$formatters.toDate(this.item.release_date)
          },
          { key: 'property.type', value: this.item.album_type },
          { key: 'property.path', value: this.item.uri }
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
