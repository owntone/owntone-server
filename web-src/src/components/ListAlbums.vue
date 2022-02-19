<template>
  <template v-for="album in albums" :key="album.itemId">
    <div v-if="!album.isItem && !hide_group_title" class="mt-6 mb-5 py-2">
      <span
        :id="'index_' + album.groupKey"
        class="tag is-info is-light is-small has-text-weight-bold"
        >{{ album.groupKey }}</span
      >
    </div>
    <div v-else-if="album.isItem" class="media" @click="open_album(album.item)">
      <div v-if="is_visible_artwork" class="media-left fd-has-action">
        <p class="image is-64x64 fd-has-shadow fd-has-action">
          <figure>
            <img
              v-lazy="{
                src: artwork_url_with_size(album.item.artwork_url),
                lifecycle: artwork_options.lazy_lifecycle
              }"
              :album="album.item.name"
              :artist="album.item.artist"
            />
          </figure>
        </p>
      </div>
      <div class="media-content fd-has-action is-clipped">
        <div style="margin-top: 0.7rem">
          <h1 class="title is-6">
            {{ album.item.name }}
          </h1>
          <h2 class="subtitle is-7 has-text-grey">
            <b>{{ album.item.artist }}</b>
          </h2>
          <h2
            v-if="album.item.date_released && album.item.media_kind === 'music'"
            class="subtitle is-7 has-text-grey has-text-weight-normal"
          >
            {{ $filters.time(album.item.date_released, 'L') }}
          </h2>
        </div>
      </div>
      <div class="media-right" style="padding-top: 0.7rem">
        <a @click.prevent.stop="open_dialog(album.item)">
          <span class="icon has-text-dark"
            ><i class="mdi mdi-dots-vertical mdi-18px"
          /></span>
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
      title="Remove podcast"
      delete_action="Remove"
      @close="show_remove_podcast_modal = false"
      @delete="remove_podcast"
    >
      <template #modal-content>
        <p>Permanently remove this podcast from your library?</p>
        <p class="is-size-7">
          (This will also remove the RSS playlist
          <b>{{ rss_playlist_to_remove.name }}</b
          >.)
        </p>
      </template>
    </modal-dialog>
  </teleport>
</template>

<script>
import ModalDialogAlbum from '@/components/ModalDialogAlbum.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import webapi from '@/webapi'
import { renderSVG } from '@/lib/SVGRenderer'

export default {
  name: 'ListAlbums',
  components: { ModalDialogAlbum, ModalDialog },

  props: ['albums', 'media_kind', 'hide_group_title'],
  emits: ['play-count-changed', 'podcast-deleted'],

  data() {
    return {
      show_details_modal: false,
      selected_album: {},

      show_remove_podcast_modal: false,
      rss_playlist_to_remove: {},

      artwork_options: {
        width: 600,
        height: 600,
        font_family: 'sans-serif',
        font_size: 200,
        font_weight: 600,
        lazy_lifecycle: {
          error: (el) => {
            el.src = this.dataURI(
              el.attributes.album.value,
              el.attributes.artist.value
            )
          }
        }
      }
    }
  },

  computed: {
    is_visible_artwork() {
      return this.$store.getters.settings_option(
        'webinterface',
        'show_cover_artwork_in_album_lists'
      ).value
    },

    media_kind_resolved: function () {
      return this.media_kind ? this.media_kind : this.selected_album.media_kind
    }
  },

  methods: {
    open_album: function (album) {
      this.selected_album = album
      if (this.media_kind_resolved === 'podcast') {
        this.$router.push({ path: '/podcasts/' + album.id })
      } else if (this.media_kind_resolved === 'audiobook') {
        this.$router.push({ path: '/audiobooks/' + album.id })
      } else {
        this.$router.push({ path: '/music/albums/' + album.id })
      }
    },

    open_dialog: function (album) {
      this.selected_album = album
      this.show_details_modal = true
    },

    open_remove_podcast_dialog: function () {
      webapi
        .library_album_tracks(this.selected_album.id, { limit: 1 })
        .then(({ data }) => {
          webapi.library_track_playlists(data.items[0].id).then(({ data }) => {
            const rssPlaylists = data.items.filter((pl) => pl.type === 'rss')
            if (rssPlaylists.length !== 1) {
              this.$store.dispatch('add_notification', {
                text: 'Podcast cannot be removed. Probably it was not added as an RSS playlist.',
                type: 'danger'
              })
              return
            }

            this.rss_playlist_to_remove = rssPlaylists[0]
            this.show_remove_podcast_modal = true
            this.show_details_modal = false
          })
        })
    },

    play_count_changed: function () {
      this.$emit('play-count-changed')
    },

    remove_podcast: function () {
      this.show_remove_podcast_modal = false
      webapi
        .library_playlist_delete(this.rss_playlist_to_remove.id)
        .then(() => {
          this.$emit('podcast-deleted')
        })
    },

    artwork_url_with_size: function (artwork_url) {
      if (this.artwork_options.width > 0 && this.artwork_options.height > 0) {
        return webapi.artwork_url_append_size_params(
          artwork_url,
          this.artwork_options.width,
          this.artwork_options.height
        )
      }
      return webapi.artwork_url_append_size_params(artwork_url)
    },

    alt_text(album, artist) {
      return artist + ' - ' + album
    },

    caption(album, artist) {
      if (album) {
        return album.substring(0, 2)
      }
      if (artist) {
        return artist.substring(0, 2)
      }
      return ''
    },

    dataURI: function (album, artist) {
      const caption = this.caption(album, artist)
      const alt_text = this.alt_text(album, artist)
      return renderSVG(caption, alt_text, {
        width: this.artwork_options.width,
        height: this.artwork_options.height,
        font_family: this.artwork_options.font_family,
        font_size: this.artwork_options.font_size,
        font_weight: this.artwork_options.font_weight
      })
    }
  }
}
</script>

<style></style>
