<template>
  <content-with-heading>
    <template #heading-left>
      <p class="title is-4">
        {{ playlist.name }}
      </p>
      <p class="heading">{{ playlists.total }} playlists</p>
    </template>
    <template #content>
      <list-playlists :playlists="playlists.items" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListPlaylists from '@/components/ListPlaylists.vue'
import webapi from '@/webapi'

const dataObject = {
  load: function (to) {
    return Promise.all([
      webapi.library_playlist(to.params.playlist_id),
      webapi.library_playlist_folder(to.params.playlist_id)
    ])
  },

  set: function (vm, response) {
    vm.playlist = response[0].data
    vm.playlists = response[1].data
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
      playlists: {}
    }
  }
}
</script>

<style></style>
