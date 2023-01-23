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
            <span class="icon"
              ><mdicon name="dots-horizontal" size="16"
            /></span>
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <span class="icon"><mdicon name="play" size="16" /></span>
            <span v-text="$t('page.files.play')" />
          </a>
        </div>
      </template>
      <template #content>
        <list-directories :directories="files.directories" />
        <list-playlists :playlists="playlists_list" />
        <list-tracks
          :tracks="files.tracks.items"
          :expression="play_expression"
          :show_icon="true"
        />
        <modal-dialog-directory
          :show="show_details_modal"
          :directory="{ path: current_directory }"
          @close="show_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListDirectories from '@/components/ListDirectories.vue'
import ListPlaylists from '@/components/ListPlaylists.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogDirectory from '@/components/ModalDialogDirectory.vue'
import webapi from '@/webapi'
import { GroupByList } from '@/lib/GroupByList'

const dataObject = {
  load: function (to) {
    if (to.query.directory) {
      return webapi.library_files(to.query.directory)
    }
    return Promise.resolve()
  },

  set: function (vm, response) {
    if (response) {
      vm.files = response.data
      vm.playlists_list = new GroupByList(response.data.playlists)
    } else {
      vm.files = {
        directories: vm.$store.state.config.directories.map((dir) => {
          return { path: dir }
        }),
        tracks: { items: [] },
        playlists: { items: [] }
      }
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
      files: {
        directories: [],
        tracks: { items: [] },
        playlists: { items: [] }
      },
      playlists_list: new GroupByList(),
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
    play: function () {
      webapi.player_play_expression(this.play_expression, false)
    }
  }
}
</script>

<style></style>
