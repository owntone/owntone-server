<template>
  <div class="fd-page-with-tabs">
    <tabs-music />
    <!-- Recently added -->
    <content-with-heading>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.browse.recently-added.title')" />
        <p class="heading" v-text="$t('page.browse.albums')" />
      </template>
      <template #content>
        <list-albums :albums="recently_added" />
      </template>
      <template #footer>
        <nav class="level">
          <p class="level-item">
            <router-link
              class="button is-light is-small is-rounded"
              :to="{ name: 'music-browse-recently-added' }"
              >{{ $t('page.browse.show-more') }}</router-link
            >
          </p>
        </nav>
      </template>
    </content-with-heading>
    <!-- Recently played -->
    <content-with-heading>
      <template #heading-left>
        <p
          class="title is-4"
          v-text="$t('page.browse.recently-played.title')"
        />
        <p class="heading" v-text="$t('page.browse.tracks')" />
      </template>
      <template #content>
        <list-tracks :tracks="recently_played" />
      </template>
      <template #footer>
        <nav class="level">
          <p class="level-item">
            <router-link
              class="button is-light is-small is-rounded"
              :to="{ name: 'music-browse-recently-played' }"
              >{{ $t('page.browse.show-more') }}</router-link
            >
          </p>
        </nav>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupByList } from '@/lib/GroupByList'
import ListAlbums from '@/components/ListAlbums.vue'
import ListTracks from '@/components/ListTracks.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return Promise.all([
      webapi.search({
        type: 'album',
        expression:
          'time_added after 8 weeks ago and media_kind is music having track_count > 3 order by time_added desc',
        limit: 3
      }),
      webapi.search({
        type: 'track',
        expression:
          'time_played after 8 weeks ago and media_kind is music order by time_played desc',
        limit: 3
      })
    ])
  },

  set(vm, response) {
    vm.recently_added = new GroupByList(response[0].data.albums)
    vm.recently_played = new GroupByList(response[1].data.tracks)
  }
}

export default {
  name: 'PageBrowse',
  components: { ContentWithHeading, ListAlbums, ListTracks, TabsMusic },

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
      recently_added: [],
      recently_played: { items: [] },
      selected_track: {},
      show_track_details_modal: false
    }
  }
}
</script>

<style></style>
