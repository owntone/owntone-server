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
import { useI18n } from 'vue-i18n'

const props = defineProps({
  item: { required: true, type: Object },
  show: Boolean,
  uris: { default: '', type: String }
})

defineEmits(['close'])

const { t } = useI18n()

const playable = computed(() => ({
  name: props.item.name,
  properties: [
    { key: 'property.tracks', value: props.item.item_count },
    { key: 'property.type', value: t(`playlist.type.${props.item.type}`) },
    { key: 'property.path', value: props.item.path }
  ],
  uri: props.item.uri,
  uris: props.uris
}))
</script>
