<template>
  <template v-for="item in items" :key="item.itemId">
    <div v-if="!item.isItem" class="py-5">
      <span
        :id="`index_${item.index}`"
        class="tag is-small has-text-weight-bold"
        v-text="item.index"
      />
    </div>
    <div
      v-else
      class="media is-align-items-center is-clickable mb-0"
      @click="open(item.item)"
    >
      <control-image
        v-if="settingsStore.show_cover_artwork_in_album_lists"
        :url="item.item.artwork_url"
        :artist="item.item.artist"
        :album="item.item.name"
        class="media-left is-small"
      />
      <div class="media-content">
        <div class="is-size-6 has-text-weight-bold" v-text="item.item.name" />
        <div
          class="is-size-7 has-text-grey has-text-weight-bold"
          v-text="item.item.artist"
        />
        <div
          v-if="item.item.date_released && item.item.media_kind === 'music'"
          class="is-size-7 has-text-grey"
          v-text="$filters.toDate(item.item.date_released)"
        />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="openDialog(item.item)">
          <mdicon class="icon has-text-grey" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
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
            <b v-text="rss_playlist_to_remove.name" />
          </template>
        </i18n-t>
      </template>
    </modal-dialog>
  </teleport>
</template>

<script>
import ControlImage from '@/components/ControlImage.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import ModalDialogAlbum from '@/components/ModalDialogAlbum.vue'
import { useSettingsStore } from '@/stores/settings'
import webapi from '@/webapi'

export default {
  name: 'ListAlbums',
  components: { ControlImage, ModalDialog, ModalDialogAlbum },
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
      rss_playlist_to_remove: {},
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
    openDialog(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    },
    openRemovePodcastDialog() {
      webapi
        .library_album_tracks(this.selectedItem.id, { limit: 1 })
        .then(({ data: album }) => {
          webapi.library_track_playlists(album.items[0].id).then(({ data }) => {
            ;[this.rss_playlist_to_remove] = data.items.filter(
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
      webapi
        .library_playlist_delete(this.rss_playlist_to_remove.id)
        .then(() => {
          this.$emit('podcast-deleted')
        })
    }
  }
}
</script>
