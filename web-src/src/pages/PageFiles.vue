<template>
  <div class="fd-page">
    <content-with-heading>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.files.title')" />
        <p class="title is-7 has-text-grey" v-text="current_directory" />
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
        <list-directories :directories="dirs" />
        <list-playlists :playlists="playlists" />
        <list-tracks
          :tracks="tracks"
          :expression="play_expression"
          :show_icon="true"
        />
        <modal-dialog-directory
          :show="show_details_modal"
          :directory="current_directory"
          @close="show_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupByList } from '@/lib/GroupByList'
import ListDirectories from '@/components/ListDirectories.vue'
import ListPlaylists from '@/components/ListPlaylists.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogDirectory from '@/components/ModalDialogDirectory.vue'
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
      vm.dirs = response.data.directories
      vm.playlists = new GroupByList(response.data.playlists)
      vm.tracks = new GroupByList(response.data.tracks)
    } else {
      vm.dirs = vm.$store.state.config.directories.map((dir) => {
        return { path: dir }
      })
      vm.playlists = new GroupByList()
      vm.tracks = new GroupByList()
    }
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
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  },

  data() {
    return {
      dirs: [],
      playlists: new GroupByList(),
      tracks: new GroupByList(),
      show_details_modal: false
    }
  },

  computed: {
    current_directory() {
      if (this.$route.query && this.$route.query.directory) {
        return this.$route.query.directory
      }
      return '/'
    },

    play_expression() {
      return (
        'path starts with "' + this.current_directory + '" order by path asc'
      )
    }
  },

  methods: {
    play() {
      webapi.player_play_expression(this.play_expression, false)
    }
  }
}
</script>

<style></style>
