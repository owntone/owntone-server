<template>
  <modal-dialog-playable
    :item="playable"
    :show="show"
    @close="$emit('close')"
  />
</template>

<script setup>
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'
import { computed } from 'vue'

const props = defineProps({
  item: { required: true, type: String },
  show: Boolean
})

defineEmits(['close'])

const playable = computed(() => ({
  expression: `path starts with "${props.item}" order by path asc`,
  name: props.item
}))
</script>
