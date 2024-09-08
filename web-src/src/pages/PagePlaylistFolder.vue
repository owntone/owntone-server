<template>
  <div>
    <content-with-heading>
      <template #heading-left>
        <p
          class="title is-4"
          v-text="
            playlist.id === 0 ? $t('page.playlists.title') : playlist.name
          "
        />
        <p
          class="heading"
          v-text="$t('page.playlists.count', { count: playlists.count })"
        />
      </template>
      <template #content>
        <list-playlists :items="playlists" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListPlaylists from '@/components/ListPlaylists.vue'
import { useConfigurationStore } from '@/stores/configuration'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_playlist(to.params.id),
      webapi.library_playlist_folder(to.params.id)
    ])
  },

  set(vm, response) {
    vm.playlist = response[0].data
    vm.playlists_list = new GroupedList(response[1].data)
  }
}

export default {
  name: 'PagePlaylistFolder',
  components: { ContentWithHeading, ListPlaylists },

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
      playlist: {},
      playlists_list: new GroupedList()
    }
  },

  computed: {
    playlists() {
      return this.playlists_list.group({
        filters: [
          (playlist) =>
            playlist.folder ||
            this.radio_playlists ||
            playlist.stream_count === 0 ||
            playlist.item_count > playlist.stream_count
        ]
      })
    },
    radio_playlists() {
      return this.configurationStore.radio_playlists
    }
  }
}
</script>
