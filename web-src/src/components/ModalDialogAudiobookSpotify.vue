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
import formatters from '@/lib/Formatters'
const props = defineProps({
  item: { required: true, type: Object },
  show: Boolean
})

defineEmits(['close'])

const playable = computed(() => ({
  image: props.item.images?.[0]?.url || '',
  name: props.item.name || '',
  properties: [
    {
      key: 'property.artist',
      value: props.item.authors?.[0]?.name
    },
    {
      key: 'property.release-date',
      value: formatters.toDate(props.item.chapters?.items?.[0]?.release_date)
    },
    { key: 'property.type', value: 'audiobook' }
  ],
  uri: props.item.uri
}))
</script>
