<template>
  <div class="fd-page">
    <content-with-hero>
      <template #heading-left>
        <h1 class="title is-5" v-text="album.name" />
        <h2 class="subtitle is-6 has-text-weight-normal">&nbsp;</h2>
        <div class="buttons fd-is-centered-mobile mt-5">
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
          :artwork_url="album.artwork_url"
          :artist="album.artist"
          :album="album.name"
          class="is-clickable fd-has-shadow fd-cover fd-cover-medium-image"
          @click="show_details_modal = true"
        />
      </template>
      <template #content>
        <p
          class="heading is-7 has-text-centered-mobile mt-5"
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
    </content-with-hero>
  </div>
</template>

<script>
import ContentWithHero from '@/templates/ContentWithHero.vue'
import CoverArtwork from '@/components/CoverArtwork.vue'
import { GroupByList } from '@/lib/GroupByList'
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
    vm.tracks = new GroupByList(response[1].data.tracks)
  }
}

export default {
  name: 'PagePodcast',
  components: {
    ContentWithHero,
    CoverArtwork,
    ListTracks,
    ModalDialog,
    ModalDialogAlbum
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
      rss_playlist_to_remove: {},
      show_details_modal: false,
      show_remove_podcast_modal: false,
      tracks: new GroupByList()
    }
  },

  computed: {
    new_tracks() {
      return this.tracks.items.filter((track) => track.play_count === 0).length
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
        this.tracks = new GroupByList(data.tracks)
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

<style></style>
