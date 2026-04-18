<template>
  <modal-dialog-playable
    :buttons="buttons"
    :item="playable"
    :show="show"
    @close="$emit('close')"
  />
</template>

<script setup>
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'
import { computed } from 'vue'
import formatters from '@/lib/formatters'
import library from '@/api/library'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'

const { t } = useI18n()

defineOptions({ name: 'ModalDialogTrack' })

const props = defineProps({
  item: { required: true, type: Object },
  show: Boolean
})

const emit = defineEmits(['close', 'play-count-changed'])

const router = useRouter()

const markAsNew = async () => {
  await library.updateTrack(props.item.id, { play_count: 'reset' })
  emit('play-count-changed')
  emit('close')
}

const markAsPlayed = async () => {
  await library.updateTrack(props.item.id, { play_count: 'increment' })
  emit('play-count-changed')
  emit('close')
}

const buttons = computed(() => {
  if (props.item.media_kind !== 'podcast') {
    return []
  } else if (props.item.play_count > 0) {
    return [{ handler: markAsNew, key: 'actions.mark-as-new' }]
  }
  return [{ handler: markAsPlayed, key: 'actions.mark-as-played' }]
})

const openAlbum = () => {
  emit('close')
  if (props.item.media_kind === 'podcast') {
    router.push({
      name: 'podcast',
      params: { id: props.item.album_id }
    })
  } else if (props.item.media_kind === 'audiobook') {
    router.push({
      name: 'audiobook-album',
      params: { id: props.item.album_id }
    })
  } else if (props.item.media_kind === 'music') {
    router.push({
      name: 'music-album',
      params: { id: props.item.album_id }
    })
  }
}

const openArtist = () => {
  emit('close')
  if (
    props.item.media_kind === 'music' ||
    props.item.media_kind === 'podcast'
  ) {
    router.push({
      name: 'music-artist',
      params: { id: props.item.album_artist_id }
    })
  } else if (props.item.media_kind === 'audiobook') {
    router.push({
      name: 'audiobook-artist',
      params: { id: props.item.album_artist_id }
    })
  }
}

const playable = computed(() => ({
  name: props.item.title,
  properties: [
    {
      handler: openAlbum,
      key: 'property.album',
      value: props.item.album
    },
    {
      handler: openArtist,
      key: 'property.album-artist',
      value: props.item.album_artist
    },
    { key: 'property.composer', value: props.item.composer },
    {
      key: 'property.release-date',
      value: formatters.toDate(props.item.date_released)
    },
    { key: 'property.year', value: props.item.year },
    { key: 'property.genre', value: props.item.genre },
    {
      key: 'property.position',
      value:
        props.item.track_number > 0 &&
        [props.item.disc_number, props.item.track_number].join(' / ')
    },
    {
      key: 'property.duration',
      value:
        props.item.length_ms > 0 && formatters.toTimecode(props.item.length_ms)
    },
    {
      key: 'property.type',
      value: `${t(`media.kind.${props.item.media_kind}`)} - ${t(`data.kind.${props.item.data_kind}`)}`
    },
    {
      key: 'property.quality',
      value:
        props.item.data_kind !== 'spotify' &&
        t('dialog.track.quality', {
          bitrate: props.item.bitrate,
          count: props.item.channels,
          format: props.item.type,
          samplerate: props.item.samplerate
        })
    },
    {
      key: 'property.added-on',
      value: formatters.toDateTime(props.item.time_added)
    },
    {
      key: 'property.rating',
      value: t('dialog.track.rating', {
        rating: Math.floor(props.item.rating / 10)
      })
    },
    { key: 'property.comment', value: props.item.comment },
    { key: 'property.path', value: props.item.path }
  ],
  uri: props.item.uri
}))
</script>
