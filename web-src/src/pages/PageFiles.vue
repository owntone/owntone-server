<template>
  <content-with-heading>
    <template #heading>
      <pane-title :content="{ title: name }" />
    </template>
    <template #actions>
      <control-button
        :button="{ handler: openDetails, icon: 'dots-horizontal' }"
      />
      <control-button
        :button="{ handler: play, icon: 'play', key: 'actions.play' }"
      />
    </template>
    <template #content>
      <list-directories :items="directories" />
      <list-playlists :items="playlists" />
      <list-tracks :items="tracks" icon="file-music-outline" />
    </template>
  </content-with-heading>
  <modal-dialog-playable
    :item="playable"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListDirectories from '@/components/ListDirectories.vue'
import ListPlaylists from '@/components/ListPlaylists.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import configuration from '@/api/configuration'
import library from '@/api/library'
import queue from '@/api/queue'
import { useConfigurationStore } from '@/stores/configuration'

export default {
  name: 'PageFiles',
  components: {
    ContentWithHeading,
    ControlButton,
    ListDirectories,
    ListPlaylists,
    ListTracks,
    ModalDialogPlayable,
    PaneTitle
  },
  beforeRouteEnter(to, from, next) {
    next(async (vm) => {
      await vm.fetchData(to)
    })
  },
  beforeRouteUpdate(to, from, next) {
    this.fetchData(to).then(() => next())
  },
  setup() {
    return {
      configurationStore: useConfigurationStore()
    }
  },
  data() {
    return {
      directories: [],
      playlists: new GroupedList(),
      showDetailsModal: false,
      tracks: new GroupedList()
    }
  },
  computed: {
    current() {
      return this.$route.query?.directory || '/'
    },
    name() {
      if (this.current !== '/') {
        return this.current?.slice(this.current.lastIndexOf('/') + 1)
      }
      return this.$t('page.files.title')
    },
    playable() {
      return {
        expression: `path starts with "${this.current}" order by path asc`,
        name: this.current,
        properties: [
          { key: 'property.folders', value: this.directories.length },
          { key: 'property.playlists', value: this.playlists.total },
          { key: 'property.tracks', value: this.tracks.total }
        ]
      }
    }
  },
  methods: {
    async fetchData(to) {
      if (to.query.directory) {
        const data = await library.files(to.query.directory)
        if (data) {
          this.directories = data.directories.map((directory) =>
            this.transform(directory.path)
          )
          this.playlists = new GroupedList(data.playlists)
          this.tracks = new GroupedList(data.tracks)
        }
      } else {
        const config = await configuration.list()
        this.directories = config.directories.map((path) =>
          this.transform(path)
        )
        this.playlists = new GroupedList()
        this.tracks = new GroupedList()
      }
    },
    openDetails() {
      this.showDetailsModal = true
    },
    play() {
      queue.playExpression(this.expression, false)
    },
    transform(path) {
      return { name: path.slice(path.lastIndexOf('/') + 1), path }
    }
  }
}
</script>
