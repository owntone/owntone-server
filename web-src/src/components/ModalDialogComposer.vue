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
        name: this.item.name,
        handler: this.open_albums,
        expression: `composer is "${this.item.name}" and media_kind is music`,
        properties: [
          {
            label: 'property.albums',
            value: this.item.album_count,
            handler: this.open_albums
          },
          {
            label: 'property.tracks',
            value: this.item.track_count,
            handler: this.open_tracks
          },
          {
            label: 'property.duration',
            value: this.$filters.toTimecode(this.item.length_ms)
          }
        ]
      }
    }
  },
  methods: {
    open_albums() {
      this.$emit('close')
      this.$router.push({
        name: 'music-composer-albums',
        params: { name: this.item.name }
      })
    },
    open_tracks() {
      this.$emit('close')
      this.$router.push({
        name: 'music-composer-tracks',
        params: { name: this.item.name }
      })
    }
  }
}
</script>
