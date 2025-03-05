<template>
  <div>
    <content-with-hero>
      <template #heading-left>
        <div class="title is-5" v-text="album.name" />
        <div class="subtitle is-6">
          <br />
        </div>
        <div
          class="is-size-7 is-uppercase has-text-centered-mobile"
          v-text="$t('count.tracks', { count: album.track_count })"
        />
        <div class="buttons is-centered-mobile mt-5">
          <control-button
            :button="{ handler: play, icon: 'play', key: 'actions.play' }"
          />
          <control-button
            :button="{ handler: showDetails, icon: 'dots-horizontal' }"
          />
        </div>
      </template>
      <template #heading-right>
        <control-image
          :url="album.artwork_url"
          :artist="album.artist"
          :album="album.name"
          class="is-clickable is-medium"
          @click="showDetails"
        />
      </template>
      <template #content>
        <list-tracks
          :items="tracks"
          :show_progress="true"
          @play-count-changed="reload_tracks"
        />
        <modal-dialog-album
          :item="album"
          :show="show_details_modal"
          :media_kind="'podcast'"
          @close="show_details_modal = false"
          @play-count-changed="reload_tracks"
          @remove-podcast="open_remove_podcast_dialog"
        />
        <modal-dialog
          :actions="actions"
          :show="show_remove_podcast_modal"
          :title="$t('page.podcast.remove-podcast')"
          @cancel="show_remove_podcast_modal = false"
          @remove="remove_podcast"
        >
          <template #content>
            <i18n-t keypath="page.podcast.remove-info" tag="p" scope="global">
              <template #separator>
                <br />
              </template>
              <template #name>
                <b v-text="rss_playlist_to_remove.name" />
              </template>
            </i18n-t>
          </template>
        </modal-dialog>
      </template>
    </content-with-hero>
  </div>
</template>

<script>
import ContentWithHero from '@/templates/ContentWithHero.vue'
import ControlButton from '@/components/ControlButton.vue'
import ControlImage from '@/components/ControlImage.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import ModalDialogAlbum from '@/components/ModalDialogAlbum.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_album(to.params.id),
      webapi.library_podcast_episodes(to.params.id)
    ])
  },

  set(vm, response) {
    vm.album = response[0].data
    vm.tracks = new GroupedList(response[1].data.tracks)
  }
}

export default {
  name: 'PagePodcast',
  components: {
    ContentWithHero,
    ControlButton,
    ControlImage,
    ListTracks,
    ModalDialog,
    ModalDialogAlbum
  },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },

  data() {
    return {
      album: {},
      rss_playlist_to_remove: {},
      show_details_modal: false,
      show_remove_podcast_modal: false,
      tracks: new GroupedList()
    }
  },
  computed: {
    actions() {
      return [
        {
          key: this.$t('page.podcast.cancel'),
          handler: 'cancel',
          icon: 'cancel'
        },
        {
          key: this.$t('page.podcast.remove'),
          handler: 'remove',
          icon: 'delete'
        }
      ]
    }
  },
  methods: {
    open_remove_podcast_dialog() {
      webapi
        .library_track_playlists(this.tracks.items[0].id)
        .then(({ data }) => {
          ;[this.rss_playlist_to_remove] = data.items.filter(
            (pl) => pl.type === 'rss'
          )
          this.show_remove_podcast_modal = true
          this.show_details_modal = false
        })
    },
    play() {
      webapi.player_play_uri(this.album.uri, false)
    },
    reload_tracks() {
      webapi.library_podcast_episodes(this.album.id).then(({ data }) => {
        this.tracks = new GroupedList(data.tracks)
      })
    },
    remove_podcast() {
      this.show_remove_podcast_modal = false
      webapi
        .library_playlist_delete(this.rss_playlist_to_remove.id)
        .then(() => {
          this.$router.replace({ name: 'podcasts' })
        })
    },
    showDetails() {
      this.show_details_modal = true
    }
  }
}
</script>
