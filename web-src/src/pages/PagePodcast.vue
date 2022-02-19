<template>
  <content-with-heading>
    <template #heading-left>
      <div class="title is-4">
        {{ album.name }}
      </div>
    </template>
    <template #heading-right>
      <div class="buttons is-centered">
        <a
          class="button is-small is-light is-rounded"
          @click="show_album_details_modal = true"
        >
          <span class="icon"
            ><i class="mdi mdi-dots-horizontal mdi-18px"
          /></span>
        </a>
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon">
            <i class="mdi mdi-play" />
          </span>
          <span>Play</span>
        </a>
      </div>
    </template>
    <template #content>
      <p class="heading has-text-centered-mobile">
        {{ album.track_count }} tracks
      </p>
      <list-tracks
        :tracks="tracks"
        :show_progress="true"
        @play-count-changed="reload_tracks"
      />
      <modal-dialog-album
        :show="show_album_details_modal"
        :album="album"
        :media_kind="'podcast'"
        :new_tracks="new_tracks"
        @close="show_album_details_modal = false"
        @play-count-changed="reload_tracks"
        @remove-podcast="open_remove_podcast_dialog"
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
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogAlbum from '@/components/ModalDialogAlbum.vue'
import ModalDialog from '@/components/ModalDialog.vue'
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
    ListTracks,
    ModalDialogAlbum,
    ModalDialog
  },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },
  beforeRouteUpdate(to, from, next) {
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  },

  data() {
    return {
      album: {},
      tracks: [],

      show_album_details_modal: false,

      show_remove_podcast_modal: false,
      rss_playlist_to_remove: {}
    }
  },

  computed: {
    new_tracks() {
      return this.tracks.filter((track) => track.play_count === 0).length
    }
  },

  methods: {
    play: function () {
      webapi.player_play_uri(this.album.uri, false)
    },

    open_remove_podcast_dialog: function () {
      this.show_album_details_modal = false
      webapi.library_track_playlists(this.tracks[0].id).then(({ data }) => {
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
      })
    },

    remove_podcast: function () {
      this.show_remove_podcast_modal = false
      webapi
        .library_playlist_delete(this.rss_playlist_to_remove.id)
        .then(() => {
          this.$router.replace({ path: '/podcasts' })
        })
    },

    reload_tracks: function () {
      webapi.library_podcast_episodes(this.album.id).then(({ data }) => {
        this.tracks = data.tracks.items
      })
    }
  }
}
</script>

<style></style>
