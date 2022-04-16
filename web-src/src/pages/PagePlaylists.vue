<template>
  <content-with-heading>
    <template #heading-left>
      <p class="title is-4">
        {{ playlist.name }}
      </p>
      <p class="heading">{{ playlists.count }} playlists</p>
    </template>
    <template #content>
      <list-playlists :playlists="playlists" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListPlaylists from '@/components/ListPlaylists.vue'
import webapi from '@/webapi'
import { GroupByList, noop } from '@/lib/GroupByList'

const dataObject = {
  load: function (to) {
    return Promise.all([
      webapi.library_playlist(to.params.playlist_id),
      webapi.library_playlist_folder(to.params.playlist_id)
    ])
  },

  set: function (vm, response) {
    vm.playlist = response[0].data
    vm.playlists_list = new GroupByList(response[1].data)
  }
}

export default {
  name: 'PagePlaylists',
  components: { ContentWithHeading, ListPlaylists },

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
      playlist: {},
      playlists_list: new GroupByList()
    }
  },

  computed: {
    radio_playlists() {
      return this.$store.state.config.radio_playlists
    },

    playlists() {
      this.playlists_list.group(noop(), [
        (playlist) =>
          playlist.folder ||
          this.radio_playlists ||
          playlist.stream_count === 0 ||
          playlist.item_count > playlist.stream_count
      ])

      return this.playlists_list
    }
  }
}
</script>

<style></style>
