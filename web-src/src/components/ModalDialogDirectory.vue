<template>
  <modal-dialog-playable
    :expression="expression"
    :show="show"
    :title="item"
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
    expression() {
      return `path starts with "${this.item}" order by path asc`
    }
  }
}
</script>
