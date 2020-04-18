<template>
  <div>
    <content-with-heading v-if="new_episodes.items.length > 0">
      <template slot="heading-left">
        <p class="title is-4">New episodes</p>
      </template>
      <template slot="heading-right">
      <div class="buttons is-centered">
        <a class="button is-small" @click="mark_all_played">
          <span class="icon">
            <i class="mdi mdi-pencil"></i>
          </span>
          <span>Mark All Played</span>
        </a>
      </div>
    </template>
    <template slot="content">
        <list-item-track v-for="track in new_episodes.items" :key="track.id" :track="track" @click="play_track(track)">
          <template slot="progress">
            <range-slider
              class="track-progress"
              min="0"
              :max="track.length_ms"
              step="1"
              :disabled="true"
              :value="track.seek_ms" >
            </range-slider>
          </template>
          <template slot="actions">
            <a @click="open_track_dialog(track)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-track>
        <modal-dialog-track :show="show_track_details_modal" :track="selected_track" @close="show_track_details_modal = false" @play_count_changed="reload_new_episodes" />
      </template>
    </content-with-heading>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Podcasts</p>
        <p class="heading">{{ albums.total }} podcasts</p>
      </template>
      <template slot="heading-right">
        <div class="buttons is-centered">
          <a class="button is-small" @click="open_add_podcast_dialog">
            <span class="icon">
              <i class="mdi mdi-rss"></i>
            </span>
            <span>Add Podcast</span>
          </a>
        </div>
      </template>
      <template slot="content">
        <list-item-album v-for="album in albums.items" :key="album.id" :album="album" :media_kind="'podcast'" @click="open_album(album)">
          <template slot="actions">
            <a @click="open_album_dialog(album)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-album>
        <modal-dialog-album
          :show="show_album_details_modal"
          :album="selected_album"
          :media_kind="'podcast'"
          @close="show_album_details_modal = false"
          @play_count_changed="reload_new_episodes"
          @remove_podcast="open_remove_podcast_dialog" />
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
        <modal-dialog-add-rss
          :show="show_url_modal"
          @close="show_url_modal = false"
          @podcast_added="reload_podcasts" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import ListItemTrack from '@/components/ListItemTrack'
import ListItemAlbum from '@/components/ListItemAlbum'
import ModalDialogTrack from '@/components/ModalDialogTrack'
import ModalDialogAlbum from '@/components/ModalDialogAlbum'
import ModalDialogAddRss from '@/components/ModalDialogAddRss'
import ModalDialog from '@/components/ModalDialog'
import RangeSlider from 'vue-range-slider'
import webapi from '@/webapi'

const albumsData = {
  load: function (to) {
    return Promise.all([
      webapi.library_podcasts(),
      webapi.library_podcasts_new_episodes()
    ])
  },

  set: function (vm, response) {
    vm.albums = response[0].data
    vm.new_episodes = response[1].data.tracks
  }
}

export default {
  name: 'PagePodcasts',
  mixins: [LoadDataBeforeEnterMixin(albumsData)],
  components: { ContentWithHeading, ListItemTrack, ListItemAlbum, ModalDialogTrack, ModalDialogAlbum, ModalDialogAddRss, ModalDialog, RangeSlider },

  data () {
    return {
      albums: {},
      new_episodes: { items: [] },

      show_album_details_modal: false,
      selected_album: {},

      show_url_modal: false,

      show_track_details_modal: false,
      selected_track: {},

      show_remove_podcast_modal: false,
      rss_playlist_to_remove: {}
    }
  },

  methods: {
    open_album: function (album) {
      this.$router.push({ path: '/podcasts/' + album.id })
    },

    play_track: function (track) {
      webapi.player_play_uri(track.uri, false)
    },

    open_track_dialog: function (track) {
      this.selected_track = track
      this.show_track_details_modal = true
    },

    open_album_dialog: function (album) {
      this.selected_album = album
      this.show_album_details_modal = true
    },

    mark_all_played: function () {
      this.new_episodes.items.forEach(ep => {
        webapi.library_track_update(ep.id, { play_count: 'increment' })
      })
      this.new_episodes.items = { }
    },

    open_add_podcast_dialog: function (item) {
      this.show_url_modal = true
    },

    open_remove_podcast_dialog: function () {
      this.show_album_details_modal = false
      webapi.library_album_tracks(this.selected_album.id, { limit: 1 }).then(({ data }) => {
        webapi.library_track_playlists(data.items[0].id).then(({ data }) => {
          const rssPlaylists = data.items.filter(pl => pl.type === 'rss')
          if (rssPlaylists.length !== 1) {
            this.$store.dispatch('add_notification', { text: 'Podcast cannot be removed. Probably it was not added as an RSS playlist.', type: 'danger' })
            return
          }

          this.rss_playlist_to_remove = rssPlaylists[0]
          this.show_remove_podcast_modal = true
        })
      })
    },

    remove_podcast: function () {
      this.show_remove_podcast_modal = false
      webapi.library_playlist_delete(this.rss_playlist_to_remove.id).then(() => {
        this.reload_podcasts()
      })
    },

    reload_new_episodes: function () {
      webapi.library_podcasts_new_episodes().then(({ data }) => {
        this.new_episodes = data.tracks
      })
    },

    reload_podcasts: function () {
      webapi.library_podcasts().then(({ data }) => {
        this.albums = data
        this.reload_new_episodes()
      })
    }
  }
}
</script>

<style>
</style>
