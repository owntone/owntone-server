<template>
  <div>
    <div v-if="is_grouped">
      <div v-for="idx in albums.indexList" :key="idx" class="mb-6">
        <span class="tag is-info is-light is-small has-text-weight-bold" :id="'index_' + idx">{{ idx }}</span>
        <list-item-album v-for="album in albums.grouped[idx]"
            :key="album.id"
            :album="album"
            @click="open_album(album)">
          <template slot="artwork" v-if="is_visible_artwork">
            <p class="image is-64x64 fd-has-shadow fd-has-action">
            <cover-artwork
                :artwork_url="album.artwork_url"
                :artist="album.artist"
                :album="album.name"
                :maxwidth="64"
                :maxheight="64" />
            </p>
          </template>
          <template slot="actions">
            <a @click="open_dialog(album)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-album>
      </div>
    </div>
    <div v-else>
      <list-item-album v-for="album in albums_list"
          :key="album.id"
          :album="album"
          @click="open_album(album)">
        <template slot="artwork" v-if="is_visible_artwork">
          <p class="image is-64x64 fd-has-shadow fd-has-action">
          <cover-artwork
              :artwork_url="album.artwork_url"
              :artist="album.artist"
              :album="album.name"
              :maxwidth="64"
              :maxheight="64" />
          </p>
        </template>
        <template slot="actions">
          <a @click="open_dialog(album)">
            <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
          </a>
        </template>
      </list-item-album>
    </div>
    <modal-dialog-album
        :show="show_details_modal"
        :album="selected_album"
        :media_kind="media_kind"
        @remove-podcast="open_remove_podcast_dialog()"
        @play-count-changed="play_count_changed()"
        @close="show_details_modal = false" />
    <modal-dialog
        :show="show_remove_podcast_modal"
        title="Remove podcast"
        delete_action="Remove"
        @close="show_remove_podcast_modal = false"
        @delete="remove_podcast">
      <template slot="modal-content">
        <p>Permanently remove this podcast from your library?</p>
        <p class="is-size-7">(This will also remove the RSS playlist <b>{{ rss_playlist_to_remove.name }}</b>.)</p>
      </template>
    </modal-dialog>
  </div>
</template>

<script>
import ListItemAlbum from '@/components/ListItemAlbum'
import ModalDialogAlbum from '@/components/ModalDialogAlbum'
import ModalDialog from '@/components/ModalDialog'
import CoverArtwork from '@/components/CoverArtwork'
import webapi from '@/webapi'
import Albums from '@/lib/Albums'

export default {
  name: 'ListAlbums',
  components: { ListItemAlbum, ModalDialogAlbum, ModalDialog, CoverArtwork },

  props: ['albums', 'media_kind'],

  data () {
    return {
      show_details_modal: false,
      selected_album: {},

      show_remove_podcast_modal: false,
      rss_playlist_to_remove: {}
    }
  },

  computed: {
    is_visible_artwork () {
      return this.$store.getters.settings_option('webinterface', 'show_cover_artwork_in_album_lists').value
    },

    media_kind_resolved: function () {
      return this.media_kind ? this.media_kind : this.selected_album.media_kind
    },

    albums_list: function () {
      if (Array.isArray(this.albums)) {
        return this.albums
      }
      return this.albums.sortedAndFiltered
    },

    is_grouped: function () {
      return (this.albums instanceof Albums && this.albums.options.group)
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
      webapi.library_album_tracks(this.selected_album.id, { limit: 1 }).then(({ data }) => {
        webapi.library_track_playlists(data.items[0].id).then(({ data }) => {
          const rssPlaylists = data.items.filter(pl => pl.type === 'rss')
          if (rssPlaylists.length !== 1) {
            this.$store.dispatch('add_notification', { text: 'Podcast cannot be removed. Probably it was not added as an RSS playlist.', type: 'danger' })
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
      webapi.library_playlist_delete(this.rss_playlist_to_remove.id).then(() => {
        this.$emit('podcast-deleted')
      })
    }
  }
}
</script>

<style>
</style>
