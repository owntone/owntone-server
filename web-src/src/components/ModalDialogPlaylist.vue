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
  name: 'ModalDialogPlaylist',
  components: { ModalDialogPlayable },
  props: {
    item: { required: true, type: Object },
    show: Boolean,
    uris: { default: '', type: String }
  },
  emits: ['close'],
  computed: {
    playable() {
      return {
        name: this.item.name,
        properties: [
          { key: 'property.tracks', value: this.item.item_count },
          {
            key: 'property.type',
            value: this.$t(`playlist.type.${this.item.type}`)
          },
          { key: 'property.path', value: this.item.path }
        ],
        uri: this.item.uri,
        uris: this.uris
      }
    }
  }
}
</script>
