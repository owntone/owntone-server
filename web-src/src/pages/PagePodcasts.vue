<template>
  <content-with-heading v-if="tracks.items.length > 0">
    <template #heading>
      <heading-title :content="{ title: $t('page.podcasts.new-episodes') }" />
    </template>
    <template #actions>
      <control-button
        :button="{
          handler: markAllAsPlayed,
          icon: 'pencil',
          key: 'actions.mark-all-played'
        }"
      />
    </template>
    <template #content>
      <list-tracks
        :items="tracks"
        :show-progress="true"
        @play-count-changed="reloadNewEpisodes"
      />
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <heading-title :content="heading" />
    </template>
    <template #actions>
      <control-button
        v-if="rss.tracks > 0"
        :button="{
          handler: updateRss,
          icon: 'refresh',
          key: 'actions.update'
        }"
      />
      <control-button
        :button="{
          handler: openAddPodcastDialog,
          icon: 'rss',
          key: 'actions.add'
        }"
      />
    </template>
    <template #content>
      <list-albums
        :items="albums"
        @play-count-changed="reloadNewEpisodes()"
        @podcast-deleted="reloadPodcasts()"
      />
    </template>
  </content-with-heading>
  <modal-dialog-add-rss
    :show="showAddPodcastModal"
    @close="showAddPodcastModal = false"
    @podcast-added="reloadPodcasts()"
  />
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogAddRss from '@/components/ModalDialogAddRss.vue'
import { useLibraryStore } from '@/stores/library'
import { useUIStore } from '@/stores/ui'
import webapi from '@/webapi'

export default {
  name: 'PagePodcasts',
  components: {
    ContentWithHeading,
    ControlButton,
    HeadingTitle,
    ListAlbums,
    ListTracks,
    ModalDialogAddRss
  },
  beforeRouteEnter(to, from, next) {
    Promise.all([
      webapi.library_albums('podcast'),
      webapi.library_podcasts_new_episodes()
    ]).then(([albums, episodes]) => {
      next((vm) => {
        vm.albums = new GroupedList(albums.data)
        vm.tracks = new GroupedList(episodes.data.tracks)
      })
    })
  },
  setup() {
    return { libraryStore: useLibraryStore(), uiStore: useUIStore() }
  },
  data() {
    return {
      albums: [],
      showAddPodcastModal: false,
      tracks: { items: [] }
    }
  },
  computed: {
    heading() {
      if (this.albums.total) {
        return {
          subtitle: [{ count: this.albums.count, key: 'count.podcasts' }],
          title: this.$t('page.podcasts.title')
        }
      }
      return {}
    },
    rss() {
      return this.libraryStore.rss
    }
  },
  methods: {
    markAllAsPlayed() {
      this.tracks.items.forEach((ep) => {
        webapi.library_track_update(ep.id, { play_count: 'increment' })
      })
      this.tracks.items = {}
    },
    openAddPodcastDialog() {
      this.showAddPodcastModal = true
    },
    reloadNewEpisodes() {
      webapi.library_podcasts_new_episodes().then(({ data }) => {
        this.tracks = new GroupedList(data.tracks)
      })
    },
    reloadPodcasts() {
      webapi.library_albums('podcast').then(({ data }) => {
        this.albums = new GroupedList(data)
        this.reloadNewEpisodes()
      })
    },
    updateRss() {
      this.libraryStore.update_dialog_scan_kind = 'rss'
      this.uiStore.showUpdateDialog = true
    }
  }
}
</script>
