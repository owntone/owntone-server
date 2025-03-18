<template>
  <content-with-heading>
    <template #heading>
      <heading-title :content="heading" />
    </template>
    <template #content>
      <list-playlists :items="playlists" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
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
    vm.playlistList = new GroupedList(response[1].data)
  }
}

export default {
  name: 'PagePlaylistFolder',
  components: { ContentWithHeading, HeadingTitle, ListPlaylists },
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
      playlistList: new GroupedList()
    }
  },
  computed: {
    heading() {
      return {
        subtitle: [{ count: this.playlists.count, key: 'count.playlists' }],
        title:
          this.playlists.count === 0
            ? this.$t('page.playlists.title')
            : this.playlist.name
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
  }
}
</script>
