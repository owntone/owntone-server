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

const openArtist = () => {
  emit('close')
  router.push({
    name: 'music-spotify-artist',
    params: { id: props.item.artists[0].id }
  })
}

const playable = computed(() => ({
  image: props.item.images?.[0]?.url || '',
  name: props.item.name || '',
  properties: [
    {
      handler: openArtist,
      key: 'property.artist',
      value: props.item.artists?.[0].name
    },
    {
      key: 'property.release-date',
      value: formatters.toDate(props.item.release_date)
    },
    { key: 'property.type', value: props.item.album_type }
  ],
  uri: props.item.uri
}))
</script>
