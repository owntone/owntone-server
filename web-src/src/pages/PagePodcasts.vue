<template>
  <content-with-heading v-if="episodes.items.length > 0">
    <template #heading>
      <pane-title :content="{ title: $t('page.podcasts.new-episodes') }" />
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
        :items="episodes"
        :show-progress="true"
        @play-count-changed="reloadEpisodes"
      />
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #actions>
      <control-button
        v-if="hasRss"
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
        @play-count-changed="reloadEpisodes"
        @podcast-deleted="reloadPodcasts"
      />
    </template>
  </content-with-heading>
  <modal-dialog-add-rss
    :show="showAddPodcastModal"
    @close="showAddPodcastModal = false"
    @podcast-added="reloadPodcasts"
  />
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import { GroupedList } from '@/lib/GroupedList'
import PaneTitle from '@/components/PaneTitle.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogAddRss from '@/components/ModalDialogAddRss.vue'
import library from '@/api/library'
import { useLibraryStore } from '@/stores/library'
import { useUIStore } from '@/stores/ui'

export default {
  name: 'PagePodcasts',
  components: {
    ContentWithHeading,
    ControlButton,
    PaneTitle,
    ListAlbums,
    ListTracks,
    ModalDialogAddRss
  },
  beforeRouteEnter(to, from, next) {
    Promise.all([
      library.albums('podcast'),
      library.newPodcastEpisodes(),
      library.rssCount()
    ]).then(([albums, episodes, rssCount]) => {
      next((vm) => {
        vm.albums = new GroupedList(albums)
        vm.episodes = new GroupedList(episodes)
        vm.rssCount = rssCount
      })
    })
  },
  setup() {
    return { libraryStore: useLibraryStore(), uiStore: useUIStore() }
  },
  data() {
    return {
      albums: [],
      episodes: { items: [] },
      rssCount: {},
      showAddPodcastModal: false
    }
  },
  computed: {
    hasRss() {
      return (this.rssCount.albums ?? 0) > 0
    },
    heading() {
      if (this.albums.total) {
        return {
          subtitle: [{ count: this.albums.count, key: 'data.podcasts' }],
          title: this.$t('page.podcasts.title')
        }
      }
      return {}
    }
  },
  methods: {
    markAllAsPlayed() {
      this.episodes.items.forEach((episode) => {
        library.updateTrack(episode.id, { play_count: 'increment' })
      })
      this.episodes.items = {}
    },
    openAddPodcastDialog() {
      this.showAddPodcastModal = true
    },
    reloadEpisodes() {
      library.newPodcastEpisodes().then((episodes) => {
        this.episodes = new GroupedList(episodes)
      })
    },
    reloadPodcasts() {
      library.albums('podcast').then((albums) => {
        this.albums = new GroupedList(albums)
        this.reloadEpisodes()
        this.reloadRssCount()
      })
    },
    reloadRssCount() {
      library.rssCount().then((rssCount) => {
        this.rssCount = rssCount
      })
    },
    updateRss() {
      this.libraryStore.update_dialog_scan_kind = 'rss'
      this.uiStore.showUpdateDialog = true
    }
  }
}
</script>
