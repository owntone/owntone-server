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
import { useRouter } from 'vue-router'

const props = defineProps({
  item: { required: true, type: Object },
  show: Boolean
})

const emit = defineEmits(['close'])

const router = useRouter()

const openAuthor = () => {
  emit('close')
  router.push({
    name: 'audiobook-spotify-album',
    params: { id: props.item.id }
  })
}

const playable = computed(() => ({
  image: props.item.images?.[0]?.url || '',
  name: props.item.name || '',
  properties: [
    {
      handler: openAuthor,
      key: 'property.artist',
      value: props.item.authors?.[0]?.name
    },
    {
      key: 'property.release-date',
      value: props.item.edition
    },
    {
      key: 'property.type',
      value: 'audiobook'
    }
  ],
  uri: props.item.uri
}))
</script>

