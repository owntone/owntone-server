<template>
  <modal-dialog
    :actions="actions"
    :show="show"
    :title="item.title"
    @close="$emit('close')"
  >
    <template #content>
      <list-properties :item="playable" />
    </template>
  </modal-dialog>
</template>

<script setup>
import { computed, ref, watch } from 'vue'
import ListProperties from '@/components/ListProperties.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import formatters from '@/lib/Formatters'
import player from '@/api/player'
import queue from '@/api/queue'
import services from '@/api/services'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'

const props = defineProps({
  item: { required: true, type: Object },
  show: Boolean
})

const emit = defineEmits(['close'])

const router = useRouter()
const { t } = useI18n()

const spotifyTrack = ref({})

const openAlbum = () => {
  emit('close')
  if (props.item.data_kind === 'spotify') {
    router.push({
      name: 'music-spotify-album',
      params: { id: spotifyTrack.value.album.id }
    })
  } else if (props.item.media_kind === 'podcast') {
    router.push({ name: 'podcast', params: { id: props.item.album_id } })
  } else {
    router.push({
      name: `${props.item.media_kind}-album`,
      params: { id: props.item.album_id }
    })
  }
}

const openArtist = () => {
  emit('close')
  if (props.item.data_kind === 'spotify') {
    router.push({
      name: 'music-spotify-artist',
      params: { id: spotifyTrack.value.artists[0].id }
    })
  } else if (
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

const openGenre = () => {
  emit('close')
  router.push({
    name: 'genre-albums',
    params: { name: props.item.genre },
    query: { mediaKind: props.item.media_kind }
  })
}

const play = () => {
  emit('close')
  player.play({ item_id: props.item.id })
}

const remove = () => {
  emit('close')
  queue.remove(props.item.id)
}

const actions = computed(() => [
  { handler: remove, icon: 'trash-can-outline', key: 'actions.remove' },
  { handler: play, icon: 'play', key: 'actions.play' }
])

const playable = computed(() => ({
  name: props.item.title,
  properties: [
    { handler: openAlbum, key: 'property.album', value: props.item.album },
    {
      handler: openArtist,
      key: 'property.album-artist',
      value: props.item.album_artist
    },
    { key: 'property.composer', value: props.item.composer },
    { key: 'property.year', value: props.item.year },
    { handler: openGenre, key: 'property.genre', value: props.item.genre },
    {
      key: 'property.position',
      value:
        props.item.track_number > 0 &&
        [props.item.disc_number, props.item.track_number].join(' / ')
    },
    {
      key: 'property.duration',
      value: formatters.toTimecode(props.item.length_ms)
    },
    { key: 'property.path', value: props.item.path },
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
    }
  ],
  uri: props.item.uri
}))

watch(
  () => props.item,
  async () => {
    if (props.item?.data_kind !== 'spotify') {
      spotifyTrack.value = {}
      return
    }
    const { api } = await services.spotify.get()
    const trackId = props.item.path.slice(props.item.path.lastIndexOf(':') + 1)
    spotifyTrack.value = await api.tracks.get(trackId)
  }
)
</script>
