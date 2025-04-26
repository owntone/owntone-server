<template>
  <list-item
    v-for="item in items"
    :key="item.itemId"
    :is-item="item.isItem"
    :image="image(item)"
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
    :media-kind="mediaKind"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
    @play-count-changed="playCountChanged"
    @podcast-deleted="podcastDeleted"
  />
</template>

<script>
import ListItem from '@/components/ListItem.vue'
import ModalDialogAlbum from '@/components/ModalDialogAlbum.vue'
import { useSettingsStore } from '@/stores/settings'

export default {
  name: 'ListAlbums',
  components: { ListItem, ModalDialogAlbum },
  props: {
    items: { required: true, type: Object },
    mediaKind: { default: '', type: String }
  },
  emits: ['play-count-changed', 'podcast-deleted'],
  setup() {
    return { settingsStore: useSettingsStore() }
  },
  data() {
    return {
      selectedItem: {},
      showDetailsModal: false
    }
  },
  computed: {
    media_kind_resolved() {
      return this.mediaKind || this.selectedItem.media_kind
    }
  },
  methods: {
    image(item) {
      if (this.settingsStore.show_cover_artwork_in_album_lists) {
        return { url: item.item.artwork_url, caption: item.item.name }
      }
      return null
    },
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
    playCountChanged() {
      this.$emit('play-count-changed')
    },
    podcastDeleted() {
      this.$emit('podcast-deleted')
    }
  }
}
</script>
