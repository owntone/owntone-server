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
  name: 'ModalDialogGenre',
  components: { ModalDialogPlayable },
  props: {
    item: { required: true, type: Object },
    mediaKind: { required: true, type: String },
    show: Boolean
  },
  emits: ['close'],
  computed: {
    playable() {
      return {
        expression: `genre is "${this.item.name}" and media_kind is ${this.mediaKind}`,
        name: this.item.name,
        properties: [
          { key: 'property.albums', value: this.item.album_count },
          { key: 'property.tracks', value: this.item.track_count },
          {
            key: 'property.duration',
            value: this.$filters.toTimecode(this.item.length_ms)
          }
        ]
      }
    }
  }
}
</script>
