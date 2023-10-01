<template>
  <template v-for="album in albums" :key="album.itemId">
    <div v-if="!album.isItem && !hide_group_title" class="mt-6 mb-5 py-2">
      <span
        :id="'index_' + album.groupKey"
        class="tag is-info is-light is-small has-text-weight-bold"
        v-text="album.groupKey"
      />
    </div>
    <div
      v-else-if="album.isItem"
      class="media is-align-items-center"
      @click="open_album(album.item)"
    >
      <div v-if="is_visible_artwork" class="media-left">
        <cover-artwork
          :artwork_url="album.item.artwork_url"
          :artist="album.item.artist"
          :album="album.item.name"
          class="is-clickable fd-has-shadow fd-cover fd-cover-small-image"
        />
      </div>
      <div class="media-content is-clickable is-clipped">
        <div>
          <h1 class="title is-6" v-text="album.item.name" />
          <h2 class="subtitle is-7 has-text-grey">
            <b v-text="album.item.artist" />
          </h2>
          <h2
            v-if="album.item.date_released && album.item.media_kind === 'music'"
            class="subtitle is-7 has-text-grey has-text-weight-normal"
            v-text="$filters.date(album.item.date_released)"
          />
        </div>
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(album.item)">
          <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-album
      :show="show_details_modal"
      :album="selected_album"
      :media_kind="media_kind"
      @remove-podcast="open_remove_podcast_dialog()"
      @play-count-changed="play_count_changed()"
      @close="show_details_modal = false"
    />
    <modal-dialog
      :show="show_remove_podcast_modal"
      :title="$t('page.podcast.remove-podcast')"
      :delete_action="$t('page.podcast.remove')"
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
  props: ['albums', 'media_kind', 'hide_group_title'],
  emits: ['play-count-changed', 'podcast-deleted'],

  data() {
    return {
      show_details_modal: false,
      selected_album: {},
      show_remove_podcast_modal: false,
      rss_playlist_to_remove: {}
    }
  },

  computed: {
    is_visible_artwork() {
      return this.$store.getters.settings_option(
        'webinterface',
        'show_cover_artwork_in_album_lists'
      ).value
    },

    media_kind_resolved() {
      return this.media_kind ? this.media_kind : this.selected_album.media_kind
    }
  },

  methods: {
    open_album(album) {
      this.selected_album = album
      if (this.media_kind_resolved === 'podcast') {
        this.$router.push({ name: 'podcast', params: { id: album.id } })
      } else if (this.media_kind_resolved === 'audiobook') {
        this.$router.push({
          name: 'audiobooks-album',
          params: { id: album.id }
        })
      } else {
        this.$router.push({ name: 'music-album', params: { id: album.id } })
      }
    },

    open_dialog(album) {
      this.selected_album = album
      this.show_details_modal = true
    },

    open_remove_podcast_dialog() {
      webapi
        .library_album_tracks(this.selected_album.id, { limit: 1 })
        .then(({ data }) => {
          webapi.library_track_playlists(data.items[0].id).then(({ data }) => {
            this.rss_playlist_to_remove = data.items.filter(
              (pl) => pl.type === 'rss'
            )[0]
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
