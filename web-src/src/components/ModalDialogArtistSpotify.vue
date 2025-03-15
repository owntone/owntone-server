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
  name: 'ModalDialogArtistSpotify',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: Object }, show: Boolean },
  emits: ['close'],
  computed: {
    playable() {
      return {
        name: this.item.name,
        properties: [
          {
            key: 'property.popularity',
            value: [this.item.popularity, this.item.followers?.total].join(
              ' / '
            )
          },
          { key: 'property.genres', value: this.item.genres?.join(', ') }
        ],
        uri: this.item.uri
      }
    }
  }
}
</script>
