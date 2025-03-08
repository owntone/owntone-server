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
  name: 'ModalDialogDirectory',
  components: { ModalDialogPlayable },
  props: { item: { required: true, type: String }, show: Boolean },
  emits: ['close'],
  computed: {
    playable() {
      return {
        expression: `path starts with "${this.item}" order by path asc`,
        name: this.item
      }
    }
  }
}
</script>
