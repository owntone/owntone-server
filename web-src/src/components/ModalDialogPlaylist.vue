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
        handler: this.open,
        uris: this.uris,
        properties: [
          { label: 'property.tracks', value: this.item.item_count },
          {
            label: 'property.type',
            value: this.$t(`playlist.type.${this.item.type}`)
          },
          { label: 'property.path', value: this.item.path }
        ]
      }
    }
  },
  methods: {
    open() {
      this.$emit('close')
      this.$router.push({
        name: 'playlist',
        params: { id: this.item.id }
      })
    }
  }
}
</script>
