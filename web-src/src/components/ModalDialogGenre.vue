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
  mediaKind: { required: true, type: String },
  show: Boolean
})

defineEmits(['close'])

const playable = computed(() => ({
  expression: `genre is "${props.item.name}" and media_kind is ${props.mediaKind}`,
  name: props.item.name,
  properties: [
    { key: 'property.albums', value: props.item.album_count },
    { key: 'property.tracks', value: props.item.track_count },
    {
      key: 'property.duration',
      value: formatters.toTimecode(props.item.length_ms)
    }
  ]
}))
</script>
