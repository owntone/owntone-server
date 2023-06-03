<template>
  <content-with-heading>
    <template #heading-left>
      <div class="title is-4" v-text="album.name" />
    </template>
    <template #heading-right>
      <div class="buttons is-centered">
        <a
          class="button is-small is-light is-rounded"
          @click="show_details_modal = true"
        >
          <span class="icon"><mdicon name="dots-horizontal" size="16" /></span>
        </a>
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon"><mdicon name="play" size="16" /></span>
          <span v-text="$t('page.podcast.play')" />
        </a>
      </div>
    </template>
    <template #content>
      <p
        class="heading has-text-centered-mobile"
        v-text="$t('page.podcast.track-count', { count: album.track_count })"
      />
      <list-tracks
        :tracks="tracks"
        :show_progress="true"
        @play-count-changed="reload_tracks"
      />
      <modal-dialog-album
        :show="show_details_modal"
        :album="album"
        :media_kind="'podcast'"
        :new_tracks="new_tracks"
        @close="show_details_modal = false"
        @play-count-changed="reload_tracks"
        @remove-podcast="open_remove_podcast_dialog"
      />
      <modal-dialog
        :show="show_remove_podcast_modal"
        :title="$t('page.podcast.remove-podcast')"
        :delete_action="$t('page.podcast.remove')"
        @close="show_remove_podcast_modal = false"
        @delete="remove_podcast"
      >
        <template #modal-content>
          <p v-text="$t('page.podcast.remove-info-1')" />
          <p class="is-size-7">
            (<span v-text="$t('page.podcast.remove-info-2')" />
            <b v-text="rss_playlist_to_remove.name" />)
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
import { GroupByList } from '@/lib/GroupByList'

const dataObject = {
  load: function (to) {
    return Promise.all([
      webapi.library_album(to.params.album_id),
      webapi.library_podcast_episodes(to.params.album_id)
    ])
  },

  set: function (vm, response) {
    vm.album = response[0].data
    vm.tracks = new GroupByList(response[1].data.tracks)
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
      tracks: new GroupByList(),
      show_details_modal: false,
      show_remove_podcast_modal: false,
      rss_playlist_to_remove: {}
    }
  },

  computed: {
    new_tracks() {
      return this.tracks.items.filter((track) => track.play_count === 0).length
    }
  },

  methods: {
    play: function () {
      webapi.player_play_uri(this.album.uri, false)
    },

    open_remove_podcast_dialog: function () {
      webapi
        .library_track_playlists(this.tracks.items[0].id)
        .then(({ data }) => {
          this.rss_playlist_to_remove = data.items.filter(
            (pl) => pl.type === 'rss'
          )[0]
          this.show_remove_podcast_modal = true
          this.show_details_modal = false
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
        this.tracks = new GroupByList(data.tracks)
      })
    }
  }
}
</script>

<style></style>
