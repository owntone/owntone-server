<template>
  <template v-for="item in items" :key="item.itemId">
    <div v-if="!item.isItem" class="mt-6 mb-5 py-2">
      <span
        :id="`index_${item.index}`"
        class="tag is-info is-light is-small has-text-weight-bold"
        v-text="item.index"
      />
    </div>
    <div v-else class="media is-align-items-center" @click="open(item.item)">
      <div v-if="show_artwork" class="media-left">
        <cover-artwork
          :url="item.item.artwork_url"
          :artist="item.item.artist"
          :album="item.item.name"
          class="is-clickable fd-has-shadow fd-cover fd-cover-small-image"
        />
      </div>
      <div class="media-content is-clickable is-clipped">
        <div>
          <h1 class="title is-6" v-text="item.item.name" />
          <h2
            class="subtitle is-7 has-text-grey has-text-weight-bold"
            v-text="item.item.artist"
          />
          <h2
            v-if="item.item.date_released && item.item.media_kind === 'music'"
            class="subtitle is-7 has-text-grey"
            v-text="$filters.date(item.item.date_released)"
          />
        </div>
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(item.item)">
          <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-album
      :item="selected_item"
      :media_kind="media_kind"
      :show="show_details_modal"
      @close="show_details_modal = false"
      @remove-podcast="open_remove_podcast_dialog()"
      @play-count-changed="play_count_changed()"
    />
    <modal-dialog
      :close_action="$t('page.podcast.cancel')"
      :delete_action="$t('page.podcast.remove')"
      :show="show_remove_podcast_modal"
      :title="$t('page.podcast.remove-podcast')"
      @close="show_remove_podcast_modal = false"
      @delete="remove_podcast"
    >
      <template #modal-content>
        <p v-text="$t('list.albums.info-1')" />
        <p class="is-size-7">
          (<span v-text="$t('list.albums.info-2')" />
          <b v-text="rss_playlist_to_remove.name" />)
        </p>
      </template>
    </modal-dialog>
  </teleport>
</template>

<script>
import CoverArtwork from '@/components/CoverArtwork.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import ModalDialogAlbum from '@/components/ModalDialogAlbum.vue'
import webapi from '@/webapi'

export default {
  name: 'ListAlbums',
  components: { CoverArtwork, ModalDialog, ModalDialogAlbum },
  props: {
    items: { required: true, type: Object },
    media_kind: { default: '', type: String }
  },
  emits: ['play-count-changed', 'podcast-deleted'],

  data() {
    return {
      rss_playlist_to_remove: {},
      selected_item: {},
      show_details_modal: false,
      show_remove_podcast_modal: false
    }
  },

  computed: {
    media_kind_resolved() {
      return this.media_kind || this.selected_item.media_kind
    },
    show_artwork() {
      return this.$store.getters.setting(
        'webinterface',
        'show_cover_artwork_in_album_lists'
      ).value
    }
  },

  methods: {
    open(item) {
      this.selected_item = item
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
    open_dialog(item) {
      this.selected_item = item
      this.show_details_modal = true
    },
    open_remove_podcast_dialog() {
      webapi
        .library_album_tracks(this.selected_item.id, { limit: 1 })
        .then(({ data: album }) => {
          webapi.library_track_playlists(album.items[0].id).then(({ data }) => {
            ;[this.rss_playlist_to_remove] = data.items.filter(
              (playlist) => playlist.type === 'rss'
            )
            this.show_remove_podcast_modal = true
            this.show_details_modal = false
          })
        })
    },
    play_count_changed() {
      this.$emit('play-count-changed')
    },
    remove_podcast() {
      this.show_remove_podcast_modal = false
      webapi
        .library_playlist_delete(this.rss_playlist_to_remove.id)
        .then(() => {
          this.$emit('podcast-deleted')
        })
    }
  }
}
</script>

<style></style>
