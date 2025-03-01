<template>
  <div>
    <content-with-heading v-if="tracks.items.length > 0">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.podcasts.new-episodes')" />
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <control-button
            :handler="mark_all_played"
            icon="pencil"
            label="page.podcasts.mark-all-played"
          />
        </div>
      </template>
      <template #content>
        <list-tracks
          :items="tracks"
          :show_progress="true"
          @play-count-changed="reload_new_episodes"
        />
      </template>
    </content-with-heading>
    <content-with-heading>
      <template #heading-left>
        <div class="title is-4" v-text="$t('page.podcasts.title')" />
        <div
          class="is-size-7 is-uppercase"
          v-text="$t('count.podcasts', { count: albums.total })"
        />
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <control-button
            v-if="rss.tracks > 0"
            :handler="update_rss"
            icon="refresh"
            label="page.podcasts.update"
          />
          <control-button
            :handler="open_add_podcast_dialog"
            icon="rss"
            label="page.podcasts.add"
          />
        </div>
      </template>
      <template #content>
        <list-albums
          :items="albums"
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
import ControlButton from '@/components/ControlButton.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogAddRss from '@/components/ModalDialogAddRss.vue'
import { useLibraryStore } from '@/stores/library'
import { useUIStore } from '@/stores/ui'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_albums('podcast'),
      webapi.library_podcasts_new_episodes()
    ])
  },

  set(vm, response) {
    vm.albums = new GroupedList(response[0].data)
    vm.tracks = new GroupedList(response[1].data.tracks)
  }
}

export default {
  name: 'PagePodcasts',
  components: {
    ContentWithHeading,
    ControlButton,
    ListAlbums,
    ListTracks,
    ModalDialogAddRss
  },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },

  setup() {
    return { libraryStore: useLibraryStore(), uiStore: useUIStore() }
  },

  data() {
    return {
      albums: [],
      tracks: { items: [] },
      show_url_modal: false
    }
  },

  computed: {
    rss() {
      return this.libraryStore.rss
    }
  },

  methods: {
    mark_all_played() {
      this.tracks.items.forEach((ep) => {
        webapi.library_track_update(ep.id, { play_count: 'increment' })
      })
      this.tracks.items = {}
    },

    open_add_podcast_dialog() {
      this.show_url_modal = true
    },

    reload_new_episodes() {
      webapi.library_podcasts_new_episodes().then(({ data }) => {
        this.tracks = new GroupedList(data.tracks)
      })
    },

    reload_podcasts() {
      webapi.library_albums('podcast').then(({ data }) => {
        this.albums = new GroupedList(data)
        this.reload_new_episodes()
      })
    },

    update_rss() {
      this.libraryStore.update_dialog_scan_kind = 'rss'
      this.uiStore.show_update_dialog = true
    }
  }
}
</script>
