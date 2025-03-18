<template>
  <div>
    <content-with-heading>
      <template #heading>
        <heading-title :content="{ title: name }" />
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
        <list-tracks
          :expression="expression"
          :items="tracks"
          icon="file-music-outline"
        />
      </template>
    </content-with-heading>
  </div>
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
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListDirectories from '@/components/ListDirectories.vue'
import ListPlaylists from '@/components/ListPlaylists.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'
import { useConfigurationStore } from '@/stores/configuration'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    if (to.query.directory) {
      return webapi.library_files(to.query.directory)
    }
    return Promise.resolve()
  },
  set(vm, response) {
    if (response) {
      vm.directories = response.data.directories.map((directory) =>
        vm.transform(directory.path)
      )
    } else if (useConfigurationStore().directories) {
      vm.directories = useConfigurationStore().directories.map((path) =>
        vm.transform(path)
      )
    } else {
      webapi.config().then((config) => {
        vm.directories = config.data.directories.map((path) =>
          vm.transform(path)
        )
      })
    }
    vm.playlists = new GroupedList(response?.data.playlists)
    vm.tracks = new GroupedList(response?.data.tracks)
  }
}

export default {
  name: 'PageFiles',
  components: {
    ContentWithHeading,
    ControlButton,
    HeadingTitle,
    ListDirectories,
    ListPlaylists,
    ListTracks,
    ModalDialogPlayable
  },
  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },
  beforeRouteUpdate(to, from, next) {
    dataObject.load(to).then((response) => {
      dataObject.set(this, response)
      next()
    })
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
    expression() {
      return `path starts with "${this.current}" order by path asc`
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
    openDetails() {
      this.showDetailsModal = true
    },
    play() {
      webapi.player_play_expression(this.expression, false)
    },
    transform(path) {
      return { name: path.slice(path.lastIndexOf('/') + 1), path }
    }
  }
}
</script>
