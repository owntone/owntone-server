<template>
  <modal-dialog-playable
    :buttons="buttons"
    :item="playable"
    :show="show"
    @close="$emit('close')"
  />
  <modal-dialog
    :actions="actions"
    :show="showRemovePodcastModal"
    :title="$t('dialog.podcast.remove.title')"
    @close="showRemovePodcastModal = false"
    @remove="removePodcast"
  >
    <template #content>
      <i18n-t tag="p" keypath="dialog.podcast.remove.info" scope="global">
        <template #separator>
          <br />
        </template>
        <template #name>
          <b v-text="item.name" />
        </template>
      </i18n-t>
    </template>
  </modal-dialog>
</template>

<script setup>
import { computed, ref } from 'vue'
import ModalDialog from '@/components/ModalDialog.vue'
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'
import formatters from '@/lib/formatters'
import library from '@/api/library'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'

const props = defineProps({
  item: { required: true, type: Object },
  mediaKind: { default: '', type: String },
  show: Boolean
})

const emit = defineEmits(['close', 'play-count-changed', 'podcast-deleted'])

const router = useRouter()
const { t } = useI18n()

const showRemovePodcastModal = ref(false)

const cancel = () => {
  showRemovePodcastModal.value = false
}

const computedMediaKind = computed(
  () => props.mediaKind || props.item.media_kind
)

const removePodcast = async () => {
  showRemovePodcastModal.value = false
  const album = await library.albumTracks(props.item.id, { limit: 1 })
  const trackId = album.items[0].id
  const data = await library.trackPlaylists(trackId)
  const rssPlaylist = data.items.find((item) => item.type === 'rss')
  if (rssPlaylist?.id) {
    await library.playlistDelete(rssPlaylist.id)
    emit('podcast-deleted')
    emit('close')
  }
}

const actions = computed(() => [
  { handler: cancel, icon: 'cancel', key: 'actions.cancel' },
  { handler: removePodcast, icon: 'delete', key: 'actions.remove' }
])

const markAsPlayed = async () => {
  await library.updateAlbum(props.item.id, { play_count: 'played' })
  emit('play-count-changed')
  emit('close')
}

const openRemovePodcastDialog = () => {
  showRemovePodcastModal.value = true
}

const buttons = computed(() => {
  if (computedMediaKind.value === 'podcast') {
    if (props.item.data_kind === 'url') {
      return [
        { handler: markAsPlayed, key: 'actions.mark-as-played' },
        { handler: openRemovePodcastDialog, key: 'actions.remove' }
      ]
    }
    return [{ handler: markAsPlayed, key: 'actions.mark-as-played' }]
  }

  return []
})

const openArtist = () => {
  emit('close')
  router.push({
    name: `${computedMediaKind.value}-artist`,
    params: { id: props.item.artist_id }
  })
}

const playable = computed(() => ({
  image: props.item.artwork_url,
  name: props.item.name,
  properties: [
    {
      handler: openArtist,
      key: 'property.artist',
      value: props.item.artist
    },
    {
      key: 'property.release-date',
      value: formatters.toDate(props.item.date_released)
    },
    { key: 'property.year', value: props.item.year },
    { key: 'property.tracks', value: props.item.track_count },
    {
      key: 'property.duration',
      value: formatters.toTimecode(props.item.length_ms)
    },
    {
      key: 'property.type',
      value: `${t(`media.kind.${props.item.media_kind}`)} - ${t(`data.kind.${props.item.data_kind}`)}`
    },
    {
      key: 'property.added-on',
      value: formatters.toDateTime(props.item.time_added)
    }
  ],
  uri: props.item.uri
}))
</script>
