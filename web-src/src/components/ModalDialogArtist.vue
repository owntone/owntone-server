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
import formatters from '@/lib/Formatters.js'
import { useI18n } from 'vue-i18n'

const props = defineProps({
  item: { required: true, type: Object },
  show: Boolean
})

const { t } = useI18n()

defineEmits(['close'])

const playable = computed(() => ({
  name: props.item.name,
  properties: [
    { key: 'property.albums', value: props.item.album_count },
    { key: 'property.tracks', value: props.item.track_count },
    {
      key: 'property.type',
      value: t(`data.kind.${props.item.data_kind}`)
    },
    {
      key: 'property.added-on',
      value: formatters.toDateTime(props.item.time_added)
    }
  ],
  uri: props.item.uri
}))
</script>
