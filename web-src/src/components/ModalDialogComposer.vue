<template>
  <modal-dialog-playable
    :item="playable"
    :show="show"
    @close="$emit('close')"
  />
</template>

<script setup>
import ModalDialogPlayable from './ModalDialogPlayable.vue'
import { computed } from 'vue'
import formatters from '@/lib/Formatters'
import { useRouter } from 'vue-router'
const props = defineProps({
  item: { required: true, type: Object },
  show: Boolean
})

const emit = defineEmits(['close'])

const router = useRouter()

const openAlbums = () => {
  emit('close')
  router.push({
    name: 'music-composer-albums',
    params: { name: props.item.name }
  })
}

const openTracks = () => {
  emit('close')
  router.push({
    name: 'music-composer-tracks',
    params: { name: props.item.name }
  })
}

const playable = computed(() => ({
  expression: `composer is "${props.item.name}" and media_kind is music`,
  name: props.item.name,
  properties: [
    {
      handler: openAlbums,
      key: 'property.albums',
      value: props.item.album_count
    },
    {
      handler: openTracks,
      key: 'property.tracks',
      value: props.item.track_count
    },
    {
      key: 'property.duration',
      value: formatters.toTimecode(props.item.length_ms)
    }
  ]
}))
</script>
