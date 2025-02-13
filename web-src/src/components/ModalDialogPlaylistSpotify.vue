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
        handler: this.open,
        uri: this.item.uri,
        properties: [
          {
            label: 'property.owner',
            value: this.item.owner?.display_name
          },
          {
            label: 'property.tracks',
            value: this.item.tracks?.total
          },
          { label: 'property.path', value: this.item.uri }
        ]
      }
    }
  },
  methods: {
    open() {
      this.$emit('close')
      this.$router.push({
        name: 'playlist-spotify',
        params: { id: this.item.id }
      })
    }
  }
}
</script>
