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
        action: this.open,
        uris: this.uris,
        properties: [
          { label: 'dialog.playlist.tracks', value: this.item.item_count },
          {
            label: 'dialog.playlist.type',
            value: this.$t(`playlist.type.${this.item.type}`)
          },
          { label: 'dialog.playlist.path', value: this.item.path }
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
