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
  item: { required: true, type: Object },
  show: Boolean
})

defineEmits(['close'])

const playable = computed(() => ({
  image: props.item.images?.[0]?.url || '',
  name: props.item.name,
  properties: [
    { key: 'property.owner', value: props.item.owner?.display_name },
    { key: 'property.tracks', value: props.item.tracks?.total },
    { key: 'property.path', value: props.item.uri }
  ],
  uri: props.item.uri
}))
</script>
