<template>
  <content-with-heading>
    <template v-slot:heading-left>
      <div class="title is-4">{{ album.name }}
      </div>
     </template>
    <template v-slot:heading-right>
      <div class="buttons is-centered">
        <a class="button is-small is-light is-rounded" @click="show_album_details_modal = true">
          <span class="icon"><i class="mdi mdi-dots-horizontal mdi-18px"></i></span>
        </a>
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon">
            <i class="mdi mdi-play"></i>
          </span>
          <span>Play</span>
        </a>
      </div>
    </template>
    <template v-slot:content>
      <p class="heading has-text-centered-mobile">{{ album.track_count }} tracks</p>
      <list-item-track v-for="track in tracks" :key="track.id" :track="track" @click="play_track(track)">
        <template v-slot:progress>
          <progress-bar :max="track.length_ms" :value="track.seek_ms" />
        </template>
        <template v-slot:actions>
          <a @click.prevent.stop="open_dialog(track)">
            <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
          </a>
        </template>
      </list-item-track>
      <modal-dialog-track
        :show="show_details_modal"
        :track="selected_track"
        @close="show_details_modal = false"
        @play-count-changed="reload_tracks" />
      <modal-dialog-album
        :show="show_album_details_modal"
        :album="album"
        :media_kind="'podcast'"
        :new_tracks="new_tracks"
        @close="show_album_details_modal = false"
        @play-count-changed="reload_tracks"
        @remove-podcast="open_remove_podcast_dialog" />
      <modal-dialog
        :show="show_remove_podcast_modal"
        title="Remove podcast"
        delete_action="Remove"
        @close="show_remove_podcast_modal = false"
        @delete="remove_podcast">
        <template v-slot:modal-content>
          <p>Permanently remove this podcast from your library?</p>
          <p class="is-size-7">(This will also remove the RSS playlist <b>{{ rss_playlist_to_remove.name }}</b>.)</p>
        </template>
      </modal-dialog>
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListItemTrack from '@/components/ListItemTrack.vue'
import ModalDialogTrack from '@/components/ModalDialogTrack.vue'
import ModalDialogAlbum from '@/components/ModalDialogAlbum.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import ProgressBar from '@/components/ProgressBar.vue'
import webapi from '@/webapi'

const dataObject = {
  load: function (to) {
    return Promise.all([
      webapi.library_album(to.params.album_id),
      webapi.library_podcast_episodes(to.params.album_id)
    ])
  },

  set: function (vm, response) {
    vm.album = response[0].data
    vm.tracks = response[1].data.tracks.items
  }
}

export default {
  name: 'PagePodcast',
  components: {
    ContentWithHeading,
    ListItemTrack,
    ModalDialogTrack,
    ModalDialogAlbum,
    ModalDialog,
    ProgressBar
  },

  data () {
    return {
      album: {},
      tracks: [],

      show_details_modal: false,
      selected_track: {},

      show_album_details_modal: false,

      show_remove_podcast_modal: false,
      rss_playlist_to_remove: {}
    }
  },

  computed: {
    new_tracks () {
      return this.tracks.filter(track => track.play_count === 0).length
    }
  },

  methods: {
    play: function () {
      webapi.player_play_uri(this.album.uri, false)
    },

    play_track: function (track) {
      webapi.player_play_uri(track.uri, false)
    },

    open_dialog: function (track) {
      this.selected_track = track
      this.show_details_modal = true
    },

    open_remove_podcast_dialog: function () {
      this.show_album_details_modal = false
      webapi.library_track_playlists(this.tracks[0].id).then(({ data }) => {
        const rssPlaylists = data.items.filter(pl => pl.type === 'rss')
        if (rssPlaylists.length !== 1) {
          this.$store.dispatch('add_notification', { text: 'Podcast cannot be removed. Probably it was not added as an RSS playlist.', type: 'danger' })
          return
        }

        this.rss_playlist_to_remove = rssPlaylists[0]
        this.show_remove_podcast_modal = true
      })
    },

    remove_podcast: function () {
      this.show_remove_podcast_modal = false
      webapi.library_playlist_delete(this.rss_playlist_to_remove.id).then(() => {
        this.$router.replace({ path: '/podcasts' })
      })
    },

    reload_tracks: function () {
      webapi.library_podcast_episodes(this.album.id).then(({ data }) => {
        this.tracks = data.tracks.items
      })
    }
  },

  beforeRouteEnter (to, from, next) {
    dataObject.load(to).then((response) => {
      next(vm => dataObject.set(vm, response))
    })
  },
  beforeRouteUpdate (to, from, next) {
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  }
}
</script>

<style>
</style>
