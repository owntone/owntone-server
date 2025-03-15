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
  name: 'ModalDialogPlaylistSpotify',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  computed: {
    playable() {
      return {
        name: this.item.name,
        properties: [
          { key: 'property.owner', value: this.item.owner?.display_name },
          { key: 'property.tracks', value: this.item.tracks?.total },
          { key: 'property.path', value: this.item.uri }
        ],
        uri: this.item.uri
      }
    }
  }
}
</script>
