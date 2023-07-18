<template>
  <div class="fd-page">
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
        <list-playlists v-if="has_playlists" :playlists="playlists" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupByList, noop } from '@/lib/GroupByList'
import ListPlaylists from '@/components/ListPlaylists.vue'
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
    vm.playlists_list = new GroupByList(response[1].data)
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
    has_playlists() {
      return Object.keys(this.playlists_list.itemsByGroup).length > 0
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
    },
    radio_playlists() {
      return this.$store.state.config.radio_playlists
    }
  }
}
</script>

<style></style>
