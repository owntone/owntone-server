<template>
  <content-with-hero>
    <template #heading>
      <heading-hero :content="heading" />
    </template>
    <template #image>
      <control-image
        :url="album.artwork_url"
        :caption="album.name"
        class="is-clickable is-medium"
        @click="openDetails"
      />
    </template>
    <template #content>
      <list-tracks
        :items="tracks"
        :show-progress="true"
        @play-count-changed="reloadTracks"
      />
      <modal-dialog-album
        :item="album"
        :show="showDetailsModal"
        media-kind="podcast"
        @close="showDetailsModal = false"
        @play-count-changed="reloadTracks"
        @remove-podcast="openRemovePodcastDialog"
      />
      <modal-dialog
        :actions="actions"
        :show="showRemovePodcastModal"
        :title="$t('page.podcast.remove-podcast')"
        @cancel="showRemovePodcastModal = false"
        @remove="removePodcast"
      >
        <template #content>
          <i18n-t keypath="page.podcast.remove-info" tag="p" scope="global">
            <template #separator>
              <br />
            </template>
            <template #name>
              <b v-text="playlistToRemove.name" />
            </template>
          </i18n-t>
        </template>
      </modal-dialog>
    </template>
  </content-with-hero>
</template>

<script>
import ContentWithHero from '@/templates/ContentWithHero.vue'
import ControlImage from '@/components/ControlImage.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingHero from '@/components/HeadingHero.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialog from '@/components/ModalDialog.vue'
import ModalDialogAlbum from '@/components/ModalDialogAlbum.vue'
import webapi from '@/webapi'

export default {
  name: 'PagePodcast',
  components: {
    ContentWithHero,
    ControlImage,
    HeadingHero,
    ListTracks,
    ModalDialog,
    ModalDialogAlbum
  },
  beforeRouteEnter(to, from, next) {
    Promise.all([
      webapi.library_album(to.params.id),
      webapi.library_podcast_episodes(to.params.id)
    ]).then(([album, episodes]) => {
      next((vm) => {
        vm.album = album.data
        vm.tracks = new GroupedList(episodes.data.tracks)
      })
    })
  },
  data() {
    return {
      album: {},
      playlistToRemove: {},
      showDetailsModal: false,
      showRemovePodcastModal: false,
      tracks: new GroupedList()
    }
  },
  computed: {
    actions() {
      return [
        {
          handler: 'cancel',
          icon: 'cancel',
          key: this.$t('page.podcast.cancel')
        },
        {
          handler: 'remove',
          icon: 'delete',
          key: this.$t('page.podcast.remove')
        }
      ]
    },
    heading() {
      return {
        count: this.$t('count.tracks', { count: this.album.track_count }),
        subtitle: '',
        title: this.album.name,
        actions: [
          { handler: this.play, icon: 'play', key: 'actions.play' },
          { handler: this.openDetails, icon: 'dots-horizontal' }
        ]
      }
    }
  },
  methods: {
    openRemovePodcastDialog() {
      webapi
        .library_track_playlists(this.tracks.items[0].id)
        .then(({ data }) => {
          ;[this.playlistToRemove] = data.items.filter(
            (pl) => pl.type === 'rss'
          )
          this.showRemovePodcastModal = true
          this.showDetailsModal = false
        })
    },
    play() {
      webapi.player_play_uri(this.album.uri, false)
    },
    reloadTracks() {
      webapi.library_podcast_episodes(this.album.id).then(({ data }) => {
        this.tracks = new GroupedList(data.tracks)
      })
    },
    removePodcast() {
      this.showRemovePodcastModal = false
      webapi.library_playlist_delete(this.playlistToRemove.id).then(() => {
        this.$router.replace({ name: 'podcasts' })
      })
    },
    openDetails() {
      this.showDetailsModal = true
    }
  }
}
</script>
