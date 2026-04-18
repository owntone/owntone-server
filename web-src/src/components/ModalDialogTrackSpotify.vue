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
import { useRouter } from 'vue-router'

const props = defineProps({
  item: { required: true, type: Object },
  show: Boolean
})

const emit = defineEmits(['close'])

const router = useRouter()

const openAlbum = () => {
  emit('close')
  router.push({
    name: 'music-spotify-album',
    params: { id: props.item.album.id }
  })
}

const openArtist = () => {
  emit('close')
  router.push({
    name: 'music-spotify-artist',
    params: { id: props.item.artists[0].id }
  })
}

const playable = computed(() => {
  if (!props.item.artists) {
    return {}
  }
  return {
    name: props.item.name,
    uri: props.item.uri,
    properties: [
      {
        handler: openAlbum,
        key: 'property.album',
        value: props.item.album.name
      },
      {
        handler: openArtist,
        key: 'property.album-artist',
        value: props.item.artists[0]?.name
      },
      {
        key: 'property.release-date',
        value: formatters.toDate(props.item.album.release_date)
      },
      {
        key: 'property.position',
        value:
          props.item.track_number > 0 &&
          [props.item.disc_number, props.item.track_number].join(' / ')
      },
      {
        key: 'property.duration',
        value: formatters.toTimecode(props.item.duration_ms)
      },
      { key: 'property.path', value: props.item.uri }
    ]
  }
})
</script>
