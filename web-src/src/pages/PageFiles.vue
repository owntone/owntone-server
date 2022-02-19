<template>
  <div>
    <content-with-heading>
      <template v-slot:heading-left>
        <p class="title is-4">Files</p>
        <p class="title is-7 has-text-grey">{{ current_directory }}</p>
      </template>
      <template v-slot:heading-right>
        <div class="buttons is-centered">
          <a class="button is-small is-light is-rounded" @click="open_directory_dialog({ 'path': current_directory })">
            <span class="icon"><i class="mdi mdi-dots-horizontal mdi-18px"></i></span>
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <span class="icon"><i class="mdi mdi-play"></i></span> <span>Play</span>
          </a>
        </div>
      </template>
      <template v-slot:content>
        <div class="media" v-if="$route.query.directory" @click="open_parent_directory()">
          <figure class="media-left fd-has-action">
            <span class="icon">
              <i class="mdi mdi-subdirectory-arrow-left"></i>
            </span>
          </figure>
          <div class="media-content fd-has-action is-clipped">
            <h1 class="title is-6">..</h1>
          </div>
          <div class="media-right">
            <slot name="actions"></slot>
          </div>
        </div>

        <list-item-directory v-for="directory in files.directories" :key="directory.path" :directory="directory" @click="open_directory(directory)">
        <template v-slot:actions>
          <a @click="open_directory_dialog(directory)">
            <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
          </a>
        </template>
        </list-item-directory>

        <list-item-playlist v-for="playlist in files.playlists.items" :key="playlist.id" :playlist="playlist" @click="open_playlist(playlist)">
          <template v-slot:icon>
            <span class="icon">
              <i class="mdi mdi-library-music"></i>
            </span>
          </template>
          <template v-slot:actions>
            <a @click="open_playlist_dialog(playlist)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-playlist>

        <list-item-track v-for="(track, index) in files.tracks.items" :key="track.id" :track="track" @click="play_track(index)">
          <template v-slot:icon>
            <span class="icon">
              <i class="mdi mdi-file-outline"></i>
            </span>
          </template>
          <template v-slot:actions>
            <a @click="open_track_dialog(track)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-track>

        <modal-dialog-directory :show="show_directory_details_modal" :directory="selected_directory" @close="show_directory_details_modal = false" />
        <modal-dialog-playlist :show="show_playlist_details_modal" :playlist="selected_playlist" @close="show_playlist_details_modal = false" />
        <modal-dialog-track :show="show_track_details_modal" :track="selected_track" @close="show_track_details_modal = false" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListItemDirectory from '@/components/ListItemDirectory.vue'
import ListItemPlaylist from '@/components/ListItemPlaylist.vue'
import ListItemTrack from '@/components/ListItemTrack.vue'
import ModalDialogDirectory from '@/components/ModalDialogDirectory.vue'
import ModalDialogPlaylist from '@/components/ModalDialogPlaylist.vue'
import ModalDialogTrack from '@/components/ModalDialogTrack.vue'
import webapi from '@/webapi'

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
    } else {
      vm.files = {
        directories: vm.$store.state.config.directories.map(dir => { return { path: dir } }),
        tracks: { items: [] },
        playlists: { items: [] }
      }
    }
  }
}

export default {
  name: 'PageFiles',
  components: { ContentWithHeading, ListItemDirectory, ListItemPlaylist, ListItemTrack, ModalDialogDirectory, ModalDialogPlaylist, ModalDialogTrack },

  data () {
    return {
      files: { directories: [], tracks: { items: [] }, playlists: { items: [] } },

      show_directory_details_modal: false,
      selected_directory: {},

      show_playlist_details_modal: false,
      selected_playlist: {},

      show_track_details_modal: false,
      selected_track: {}
    }
  },

  computed: {
    current_directory () {
      if (this.$route.query && this.$route.query.directory) {
        return this.$route.query.directory
      }
      return '/'
    }
  },

  methods: {
    open_parent_directory: function () {
      const parent = this.current_directory.slice(0, this.current_directory.lastIndexOf('/'))
      if (parent === '' || this.$store.state.config.directories.includes(this.current_directory)) {
        this.$router.push({ path: '/files' })
      } else {
        this.$router.push({ path: '/files', query: { directory: this.current_directory.slice(0, this.current_directory.lastIndexOf('/')) } })
      }
    },

    open_directory: function (directory) {
      this.$router.push({ path: '/files', query: { directory: directory.path } })
    },

    open_directory_dialog: function (directory) {
      this.selected_directory = directory
      this.show_directory_details_modal = true
    },

    play: function () {
      webapi.player_play_expression('path starts with "' + this.current_directory + '" order by path asc', false)
    },

    play_track: function (position) {
      webapi.player_play_uri(this.files.tracks.items.map(a => a.uri).join(','), false, position)
    },

    open_track_dialog: function (track) {
      this.selected_track = track
      this.show_track_details_modal = true
    },

    open_playlist: function (playlist) {
      this.$router.push({ path: '/playlists/' + playlist.id + '/tracks' })
    },

    open_playlist_dialog: function (playlist) {
      this.selected_playlist = playlist
      this.show_playlist_details_modal = true
    }
  },

  beforeRouteEnter (to, from, next) {
    dataObject.load(to).then((response) => {
      next(vm => dataObject.set(vm, response))
    })
  },
  beforeRouteUpdate (to, from, next) {
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  }
}
</script>

<style>
</style>
