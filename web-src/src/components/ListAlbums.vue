<template>
  <list-item
    v-for="item in items"
    :key="item.itemId"
    :is-item="item.isItem"
    :image="url(item)"
    :index="item.index"
    :lines="[
      item.item.name,
      item.item.artist,
      $filters.toDate(item.item.date_released)
    ]"
    @open="open(item.item)"
    @open-details="openDetails(item.item)"
  />
  <modal-dialog-album
    :item="selectedItem"
    :media_kind="media_kind"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
    @remove-podcast="openRemovePodcastDialog()"
    @play-count-changed="onPlayCountChange()"
  />
  <modal-dialog
    :actions="actions"
    :show="showRemovePodcastModal"
    :title="$t('page.podcast.remove-podcast')"
    @cancel="showRemovePodcastModal = false"
    @remove="removePodcast"
  >
    <template #content>
      <i18n-t keypath="list.albums.info" tag="p" scope="global">
        <template #separator>
          <br />
        </template>
        <template #name>
          <b v-text="playlistToRemove.name" />
        </template>
      </i18n-t>
    </template>
  </modal-dialog>
</template>

<script>
import ListItem from '@/components/ListItem.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import ModalDialogAlbum from '@/components/ModalDialogAlbum.vue'
import { useSettingsStore } from '@/stores/settings'
import webapi from '@/webapi'

export default {
  name: 'ListAlbums',
  components: { ListItem, ModalDialog, ModalDialogAlbum },
  props: {
    items: { required: true, type: Object },
    media_kind: { default: '', type: String }
  },
  emits: ['play-count-changed', 'podcast-deleted'],
  setup() {
    return { settingsStore: useSettingsStore() }
  },
  data() {
    return {
      playlistToRemove: {},
      selectedItem: {},
      showDetailsModal: false,
      showRemovePodcastModal: false
    }
  },
  computed: {
    actions() {
      return [
        { handler: 'cancel', icon: 'cancel', key: 'page.podcast.cancel' },
        { handler: 'remove', icon: 'delete', key: 'page.podcast.remove' }
      ]
    },
    media_kind_resolved() {
      return this.media_kind || this.selectedItem.media_kind
    }
  },
  methods: {
    open(item) {
      this.selectedItem = item
      if (this.media_kind_resolved === 'podcast') {
        this.$router.push({ name: 'podcast', params: { id: item.id } })
      } else if (this.media_kind_resolved === 'audiobook') {
        this.$router.push({
          name: 'audiobooks-album',
          params: { id: item.id }
        })
      } else {
        this.$router.push({ name: 'music-album', params: { id: item.id } })
      }
    },
    openDetails(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    },
    openRemovePodcastDialog() {
      webapi
        .library_album_tracks(this.selectedItem.id, { limit: 1 })
        .then(({ data: album }) => {
          webapi.library_track_playlists(album.items[0].id).then(({ data }) => {
            ;[this.playlistToRemove] = data.items.filter(
              (playlist) => playlist.type === 'rss'
            )
            this.showRemovePodcastModal = true
            this.showDetailsModal = false
          })
        })
    },
    onPlayCountChange() {
      this.$emit('play-count-changed')
    },
    removePodcast() {
      this.showRemovePodcastModal = false
      webapi.library_playlist_delete(this.playlistToRemove.id).then(() => {
        this.$emit('podcast-deleted')
      })
    },
    url(item) {
      if (this.settingsStore.show_cover_artwork_in_album_lists) {
        return item.item.artwork_url
      }
      return null
    }
  }
}
</script>
