<template>
  <content-with-heading>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-playlists :items="playlists" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import PaneTitle from '@/components/PaneTitle.vue'
import ListPlaylists from '@/components/ListPlaylists.vue'
import library from '@/api/library'
import { useConfigurationStore } from '@/stores/configuration'

export default {
  name: 'PagePlaylistFolder',
  components: { ContentWithHeading, PaneTitle, ListPlaylists },
  beforeRouteEnter(to, from, next) {
    next(async (vm) => {
      await vm.fetchData(to.params.id)
    })
  },
  beforeRouteUpdate(to, from, next) {
    this.fetchData(to.params.id).then(() => next())
  },
  setup() {
    return {
      configurationStore: useConfigurationStore()
    }
  },
  data() {
    return {
      playlist: {},
      playlistList: new GroupedList()
    }
  },
  computed: {
    heading() {
      return {
        subtitle: [{ count: this.playlists.count, key: 'data.playlists' }],
        title: this.$t('page.playlists.title', this.playlists.count, {
          name: this.playlist.name
        })
      }
    },
    playlists() {
      return this.playlistList.group({
        filters: [
          (playlist) =>
            playlist.folder ||
            this.configurationStore.radio_playlists ||
            playlist.stream_count === 0 ||
            playlist.item_count > playlist.stream_count
        ]
      })
    }
  },
  methods: {
    async fetchData(id) {
      const [playlist, playlistFolder] = await Promise.all([
        library.playlist(id),
        library.playlistFolder(id)
      ])
      this.playlist = playlist
      this.playlistList = new GroupedList(playlistFolder)
    }
  }
}
</script>
