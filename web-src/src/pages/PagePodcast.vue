<template>
  <div>
    <content-with-hero>
      <template #heading-left>
        <div class="title is-5" v-text="album.name" />
        <div class="subtitle is-6">
          <br />
        </div>
        <div class="buttons is-centered-mobile mt-5">
          <a class="button is-small is-dark is-rounded" @click="play">
            <mdicon class="icon" name="play" size="16" />
            <span v-text="$t('page.podcast.play')" />
          </a>
          <a
            class="button is-small is-light is-rounded"
            @click="show_details_modal = true"
          >
            <mdicon class="icon" name="dots-horizontal" size="16" />
          </a>
        </div>
      </template>
      <template #heading-right>
        <cover-artwork
          :url="album.artwork_url"
          :artist="album.artist"
          :album="album.name"
          class="is-clickable is-medium"
          @click="show_details_modal = true"
        />
      </template>
      <template #content>
        <div
          class="is-size-7 is-uppercase has-text-centered-mobile my-5"
          v-text="$t('page.podcast.track-count', { count: album.track_count })"
        />
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
        <modal-dialog-action
          :show="show_remove_podcast_modal"
          :title="$t('page.podcast.remove-podcast')"
          :close_action="$t('page.podcast.cancel')"
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
        </modal-dialog-action>
      </template>
    </content-with-hero>
  </div>
</template>

<script>
import ContentWithHero from '@/templates/ContentWithHero.vue'
import CoverArtwork from '@/components/CoverArtwork.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogAction from '@/components/ModalDialogAction.vue'
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
    CoverArtwork,
    ListTracks,
    ModalDialogAction,
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
    }
  }
}
</script>
