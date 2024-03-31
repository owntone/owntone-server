<template>
  <div>
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
        <list-directories :items="dirs" />
        <list-playlists :items="playlists" />
        <list-tracks
          :expression="play_expression"
          :items="tracks"
          :show_icon="true"
        />
        <modal-dialog-directory
          :item="current_directory"
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
      vm.playlists = new GroupedList(response.data.playlists)
      vm.tracks = new GroupedList(response.data.tracks)
    } else {
      vm.dirs = vm.$store.state.config.directories.map((dir) => ({ path: dir }))
      vm.playlists = new GroupedList()
      vm.tracks = new GroupedList()
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

  data() {
    return {
      dirs: [],
      playlists: new GroupedList(),
      show_details_modal: false,
      tracks: new GroupedList()
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
      return `path starts with "${this.current_directory}" order by path asc`
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
