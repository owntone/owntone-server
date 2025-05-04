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
      <i18n-t keypath="dialog.podcast.remove.info" tag="p" scope="global">
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

<script>
import ModalDialog from '@/components/ModalDialog.vue'
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'
import library from '@/api/library'

export default {
  name: 'ModalDialogAlbum',
  components: { ModalDialog, ModalDialogPlayable },
  props: {
    item: { required: true, type: Object },
    mediaKind: { default: '', type: String },
    show: Boolean
  },
  emits: ['close', 'play-count-changed', 'podcast-deleted'],
  data() {
    return {
      showRemovePodcastModal: false
    }
  },
  computed: {
    actions() {
      return [
        {
          handler: this.cancel,
          icon: 'cancel',
          key: this.$t('actions.cancel')
        },
        {
          handler: this.removePodcast,
          icon: 'delete',
          key: this.$t('actions.remove')
        }
      ]
    },
    buttons() {
      if (this.mediaKindResolved === 'podcast') {
        if (this.item.data_kind === 'url') {
          return [
            { handler: this.markAsPlayed, key: 'actions.mark-as-played' },
            {
              handler: this.openRemovePodcastDialog,
              key: 'actions.remove'
            }
          ]
        }
        return [{ handler: this.markAsPlayed, key: 'actions.mark-as-played' }]
      }
      return []
    },
    mediaKindResolved() {
      return this.mediaKind || this.item.media_kind
    },
    playable() {
      return {
        image: this.item.artwork_url,
        name: this.item.name,
        properties: [
          {
            handler: this.openArtist,
            key: 'property.artist',
            value: this.item.artist
          },
          {
            key: 'property.release-date',
            value: this.$formatters.toDate(this.item.date_released)
          },
          { key: 'property.year', value: this.item.year },
          { key: 'property.tracks', value: this.item.track_count },
          {
            key: 'property.duration',
            value: this.$formatters.toTimecode(this.item.length_ms)
          },
          {
            key: 'property.type',
            value: `${this.$t(`media.kind.${this.item.media_kind}`)} - ${this.$t(`data.kind.${this.item.data_kind}`)}`
          },
          {
            key: 'property.added-on',
            value: this.$formatters.toDateTime(this.item.time_added)
          }
        ],
        uri: this.item.uri
      }
    }
  },
  methods: {
    cancel() {
      this.showRemovePodcastModal = false
    },
    markAsPlayed() {
      library.updateAlbum(this.item.id, { play_count: 'played' }).then(() => {
        this.$emit('play-count-changed')
        this.$emit('close')
      })
    },
    openArtist() {
      this.$emit('close')
      if (this.mediaKindResolved === 'audiobook') {
        this.$router.push({
          name: 'audiobooks-artist',
          params: { id: this.item.artist_id }
        })
      } else {
        this.$router.push({
          name: 'music-artist',
          params: { id: this.item.artist_id }
        })
      }
    },
    openRemovePodcastDialog() {
      this.showRemovePodcastModal = true
      this.showDetailsModal = false
    },
    removePodcast() {
      this.showRemovePodcastModal = false
      library.albumTracks(this.item.id, { limit: 1 }).then((album) => {
        library.trackPlaylists(album.items[0].id).then((data) => {
          const { id } = data.items.find((item) => item.type === 'rss')
          library.playlistDelete(id).then(() => {
            this.$emit('podcast-deleted')
            this.$emit('close')
          })
        })
      })
    }
  }
}
</script>
