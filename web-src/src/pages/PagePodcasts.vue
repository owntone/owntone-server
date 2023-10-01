<template>
  <div class="fd-page">
    <content-with-heading v-if="new_episodes.items.length > 0">
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.podcasts.new-episodes')" />
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <a class="button is-small" @click="mark_all_played">
            <mdicon class="icon" name="pencil" size="16" />
            <span v-text="$t('page.podcasts.mark-all-played')" />
          </a>
        </div>
      </template>
      <template #content>
        <list-tracks
          :tracks="new_episodes"
          :show_progress="true"
          @play-count-changed="reload_new_episodes"
        />
      </template>
    </content-with-heading>
    <content-with-heading>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.podcasts.title')" />
        <p
          class="heading"
          v-text="$t('page.podcasts.count', { count: albums.total })"
        />
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <a v-if="rss.tracks > 0" class="button is-small" @click="update_rss">
            <mdicon class="icon" name="refresh" size="16" />
            <span v-text="$t('page.podcasts.update')" />
          </a>
          <a class="button is-small" @click="open_add_podcast_dialog">
            <mdicon class="icon" name="rss" size="16" />
            <span v-text="$t('page.podcasts.add')" />
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
import * as types from '@/store/mutation_types'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupByList } from '@/lib/GroupByList'
import ListAlbums from '@/components/ListAlbums.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogAddRss from '@/components/ModalDialogAddRss.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.library_albums('podcast'),
      webapi.library_podcasts_new_episodes()
    ])
  },

  set(vm, response) {
    vm.albums = new GroupByList(response[0].data)
    vm.new_episodes = new GroupByList(response[1].data.tracks)
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
    mark_all_played() {
      this.new_episodes.items.forEach((ep) => {
        webapi.library_track_update(ep.id, { play_count: 'increment' })
      })
      this.new_episodes.items = {}
    },

    open_add_podcast_dialog(item) {
      this.show_url_modal = true
    },

    reload_new_episodes() {
      webapi.library_podcasts_new_episodes().then(({ data }) => {
        this.new_episodes = new GroupByList(data.tracks)
      })
    },

    reload_podcasts() {
      webapi.library_albums('podcast').then(({ data }) => {
        this.albums = new GroupByList(data)
        this.reload_new_episodes()
      })
    },

    update_rss() {
      this.$store.commit(types.UPDATE_DIALOG_SCAN_KIND, 'rss')
      this.$store.commit(types.SHOW_UPDATE_DIALOG, true)
    }
  }
}
</script>

<style></style>
