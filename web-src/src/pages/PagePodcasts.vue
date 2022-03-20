<template>
  <div>
    <content-with-heading v-if="new_episodes.items.length > 0">
      <template #heading-left>
        <p class="title is-4">New episodes</p>
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <a class="button is-small" @click="mark_all_played">
            <span class="icon">
              <i class="mdi mdi-pencil" />
            </span>
            <span>Mark All Played</span>
          </a>
        </div>
      </template>
      <template #content>
        <list-tracks
          :tracks="new_episodes.items"
          :show_progress="true"
          @play-count-changed="reload_new_episodes"
        />
      </template>
    </content-with-heading>

    <content-with-heading>
      <template #heading-left>
        <p class="title is-4">Podcasts</p>
        <p class="heading">{{ albums.total }} podcasts</p>
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <a v-if="rss.tracks > 0" class="button is-small" @click="update_rss">
            <span class="icon">
              <i class="mdi mdi-refresh" />
            </span>
            <span>Update</span>
          </a>
          <a class="button is-small" @click="open_add_podcast_dialog">
            <span class="icon">
              <i class="mdi mdi-rss" />
            </span>
            <span>Add Podcast</span>
          </a>
        </div>
      </template>
      <template #content>
        <list-albums
          :albums="albums"
          @play-count-changed="reload_new_episodes()"
          @podcast-deleted="reload_podcasts()"
        />
        <modal-dialog-add-rss
          :show="show_url_modal"
          @close="show_url_modal = false"
          @podcast-added="reload_podcasts()"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListTracks from '@/components/ListTracks.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import ModalDialogAddRss from '@/components/ModalDialogAddRss.vue'
import * as types from '@/store/mutation_types'
import webapi from '@/webapi'
import { GroupByList } from '@/lib/GroupByList'

const dataObject = {
  load: function (to) {
    return Promise.all([
      webapi.library_albums('podcast'),
      webapi.library_podcasts_new_episodes()
    ])
  },

  set: function (vm, response) {
    vm.albums = new GroupByList(response[0].data)
    vm.new_episodes = response[1].data.tracks
  }
}

export default {
  name: 'PagePodcasts',
  components: {
    ContentWithHeading,
    ListTracks,
    ListAlbums,
    ModalDialogAddRss
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
      albums: [],
      new_episodes: { items: [] },

      show_url_modal: false
    }
  },

  computed: {
    rss() {
      return this.$store.state.rss_count
    }
  },

  methods: {
    mark_all_played: function () {
      this.new_episodes.items.forEach((ep) => {
        webapi.library_track_update(ep.id, { play_count: 'increment' })
      })
      this.new_episodes.items = {}
    },

    open_add_podcast_dialog: function (item) {
      this.show_url_modal = true
    },

    reload_new_episodes: function () {
      webapi.library_podcasts_new_episodes().then(({ data }) => {
        this.new_episodes = data.tracks
      })
    },

    reload_podcasts: function () {
      webapi.library_albums('podcast').then(({ data }) => {
        this.albums = new GroupByList(data)
        this.reload_new_episodes()
      })
    },

    update_rss: function () {
      this.$store.commit(types.UPDATE_DIALOG_SCAN_KIND, 'rss')
      this.$store.commit(types.SHOW_UPDATE_DIALOG, true)
    }
  }
}
</script>

<style></style>
