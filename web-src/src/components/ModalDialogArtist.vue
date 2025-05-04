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
  name: 'ModalDialogArtist',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  computed: {
    playable() {
      return {
        name: this.item.name,
        properties: [
          { key: 'property.albums', value: this.item.album_count },
          { key: 'property.tracks', value: this.item.track_count },
          {
            key: 'property.type',
            value: this.$t(`data.kind.${this.item.data_kind}`)
          },
          {
            key: 'property.added-on',
            value: this.$formatters.toDateTime(this.item.time_added)
          }
        ],
        uri: this.item.uri
      }
    }
  }
}
</script>
