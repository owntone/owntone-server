<template>
  <modal-dialog-playable
    :item="playable"
    :show="show"
    @close="$emit('close')"
  />
</template>

<script>
import ModalDialogPlayable from './ModalDialogPlayable.vue'

export default {
  name: 'ModalDialogComposer',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  computed: {
    playable() {
      return {
        expression: `composer is "${this.item.name}" and media_kind is music`,
        name: this.item.name,
        properties: [
          {
            handler: this.openAlbums,
            key: 'property.albums',
            value: this.item.album_count
          },
          {
            handler: this.openTracks,
            key: 'property.tracks',
            value: this.item.track_count
          },
          {
            key: 'property.duration',
            value: this.$formatters.toTimecode(this.item.length_ms)
          }
        ]
      }
    }
  },
  methods: {
    openAlbums() {
      this.$emit('close')
      this.$router.push({
        name: 'music-composer-albums',
        params: { name: this.item.name }
      })
    },
    openTracks() {
      this.$emit('close')
      this.$router.push({
        name: 'music-composer-tracks',
        params: { name: this.item.name }
      })
    }
  }
}
</script>
