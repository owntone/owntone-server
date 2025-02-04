<template>
  <div>
    <content-with-heading>
      <template #heading-left>
        <p class="title is-4" v-text="name" />
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <a
            class="button is-small is-light is-rounded"
            @click="show_details_modal = true"
          >
            <mdicon class="icon" name="dots-horizontal" size="16" />
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <mdicon class="icon" name="play" size="16" />
            <span v-text="$t('page.files.play')" />
          </a>
        </div>
      </template>
      <template #content>
        <list-directories :items="directories" />
        <list-playlists :items="playlists" />
        <list-tracks
          :expression="play_expression"
          :items="tracks"
          :show_icon="true"
        />
        <modal-dialog-directory
          :item="current"
          :show="show_details_modal"
          @close="show_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListDirectories from '@/components/ListDirectories.vue'
import ListPlaylists from '@/components/ListPlaylists.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogDirectory from '@/components/ModalDialogDirectory.vue'
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
    ListDirectories,
    ListPlaylists,
    ListTracks,
    ModalDialogDirectory
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
      show_details_modal: false,
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
    play_expression() {
      return `path starts with "${this.current}" order by path asc`
    }
  },

  methods: {
    play() {
      webapi.player_play_expression(this.play_expression, false)
    },
    transform(path) {
      return { name: path.slice(path.lastIndexOf('/') + 1), path }
    }
  }
}
</script>
